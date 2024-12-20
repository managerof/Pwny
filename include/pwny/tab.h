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

#ifndef _TAB_H_
#define _TAB_H_

#include <unistd.h>
#include <sys/types.h>
#include <ev.h>

#include <pwny/c2.h>
#include <pwny/pipe.h>
#include <pwny/api.h>
#include <pwny/tlv.h>
#include <pwny/link.h>
#include <pwny/child.h>
#include <pwny/queue.h>

#define TAB_BASE 1
#define TAB_TERM \
        TLV_TAG_CUSTOM(API_CALL_INTERNAL, \
                       2, \
                       API_CALL)

#define TAB_EV_FLAGS EVFLAG_NOENV | EVBACKEND_SELECT

typedef struct
{
    c2_t *c2;

    api_calls_t *api_calls;
    tunnels_t *tunnels;

    struct ev_loop *loop;
} tab_t;

tab_t *tab_create(void);

void tab_read(void *data);
void tab_write(void *data);

void tab_register_call(tab_t *tab, int tag, api_t handler);
void tab_register_pipe(tab_t *tab, int type, pipe_callbacks_t callbacks);

void tab_setup(tab_t *tab);
int tab_start(tab_t *tab);
void tab_destroy(tab_t *tab);

void register_tab_api_calls(api_calls_t **api_calls);

#endif
