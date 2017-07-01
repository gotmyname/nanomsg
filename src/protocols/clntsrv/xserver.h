/*
    Copyright (c) 2012-2013 Martin Sustrik  All rights reserved.
    Copyright (c) 2017 gotmyname@outlook.com

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#ifndef NN_XSERVER_INCLUDED
#define NN_XSERVER_INCLUDED

#include "../../protocol.h"

#include "../../utils/hash.h"
#include "../../utils/queue.h"

#include "../utils/fq.h"

#include <stddef.h>

#define NN_XSERVER_OUT 1

struct nn_xserver_data {
    struct nn_pipe *pipe;
    struct nn_hash_item outitem;
    struct nn_fq_data initem;
    uint32_t flags;

    /*  Whether peername has been sent through message header. */
    int peername_sent;
};

struct nn_xserver {

    struct nn_sockbase sockbase;

    /*  Key to be assigned to the next added pipe. */
    uint32_t next_key;

    /*  Map of all registered pipes indexed by the peer ID. */
    struct nn_hash outpipes;

    /*  Fair-queuer to get messages from. */
    struct nn_fq inpipes;

    /*  Queue of just closed pipes. */
    struct nn_queue cpipes;
};

void nn_xserver_init (struct nn_xserver *self, const struct nn_sockbase_vfptr *vfptr,
    void *hint);
void nn_xserver_term (struct nn_xserver *self);

int nn_xserver_add (struct nn_sockbase *self, struct nn_pipe *pipe);
void nn_xserver_rm (struct nn_sockbase *self, struct nn_pipe *pipe);
void nn_xserver_in (struct nn_sockbase *self, struct nn_pipe *pipe);
void nn_xserver_out (struct nn_sockbase *self, struct nn_pipe *pipe);
int nn_xserver_events (struct nn_sockbase *self);
int nn_xserver_send (struct nn_sockbase *self, struct nn_msg *msg);
int nn_xserver_recv (struct nn_sockbase *self, struct nn_msg *msg);

int nn_xserver_create (void *hint, struct nn_sockbase **sockbase);
int nn_xserver_ispeer (int socktype);

#endif
