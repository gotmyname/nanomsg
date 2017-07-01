/*
    Copyright (c) 2012-2014 Martin Sustrik  All rights reserved.
    Copyright 2016 Garrett D'Amore <garrett@damore.org>
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

#include "xserver.h"

#include "../../nn.h"
#include "../../pair.h"
#include "../../clntsrv.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/random.h"
#include "../../utils/wire.h"
#include "../../utils/attr.h"

#include <string.h>

struct nn_closed_pipe {
    uint32_t key;
    struct nn_queue_item item;
};

/*  Private functions. */
static void nn_xserver_destroy (struct nn_sockbase *self);

static const struct nn_sockbase_vfptr nn_xserver_sockbase_vfptr = {
    NULL,
    nn_xserver_destroy,
    nn_xserver_add,
    nn_xserver_rm,
    nn_xserver_in,
    nn_xserver_out,
    nn_xserver_events,
    nn_xserver_send,
    nn_xserver_recv,
    NULL,
    NULL
};

void nn_xserver_init (struct nn_xserver *self, const struct nn_sockbase_vfptr *vfptr,
    void *hint)
{
    nn_sockbase_init (&self->sockbase, vfptr, hint);

    /*  Start assigning keys beginning with a random number. This way there
        are no key clashes even if the executable is re-started. */
    nn_random_generate (&self->next_key, sizeof (self->next_key));

    nn_hash_init (&self->outpipes);
    nn_fq_init (&self->inpipes);
    nn_queue_init(&self->cpipes);
}

void nn_xserver_term (struct nn_xserver *self)
{
    struct nn_queue_item *item;
    struct nn_closed_pipe *cpipe;

    while ((item = nn_queue_pop (&self->cpipes))) {
        cpipe = nn_cont (item, struct nn_closed_pipe, item);
        nn_free (cpipe);
    }
    nn_fq_term (&self->inpipes);
    nn_hash_term (&self->outpipes);
    nn_sockbase_term (&self->sockbase);
}

static void nn_xserver_destroy (struct nn_sockbase *self)
{
    struct nn_xserver *xserver;

    xserver = nn_cont (self, struct nn_xserver, sockbase);

    nn_xserver_term (xserver);
    nn_free (xserver);
}

int nn_xserver_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xserver *xserver;
    struct nn_xserver_data *data;
    int rcvprio;
    size_t sz;

    xserver = nn_cont (self, struct nn_xserver, sockbase);

    sz = sizeof (rcvprio);
    nn_pipe_getopt (pipe, NN_SOL_SOCKET, NN_RCVPRIO, &rcvprio, &sz);
    nn_assert (sz == sizeof (rcvprio));
    nn_assert (rcvprio >= 1 && rcvprio <= 16);

    data = nn_alloc (sizeof (struct nn_xserver_data), "pipe data (xserver)");
    alloc_assert (data);
    data->pipe = pipe;
    nn_hash_item_init (&data->outitem);
    data->flags = 0;
    nn_hash_insert (&xserver->outpipes, xserver->next_key & 0x7fffffff,
        &data->outitem);
    ++xserver->next_key;
    nn_fq_add (&xserver->inpipes, &data->initem, pipe, rcvprio);
    data->peername_sent = 0;
    nn_pipe_setdata (pipe, data);

    return 0;
}

void nn_xserver_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xserver *xserver;
    struct nn_xserver_data *data;
    struct nn_closed_pipe *cpipe;

    xserver = nn_cont (self, struct nn_xserver, sockbase);
    data = nn_pipe_getdata (pipe);

    nn_fq_rm (&xserver->inpipes, &data->initem);
    nn_hash_erase (&xserver->outpipes, &data->outitem);
    nn_hash_item_term (&data->outitem);

    cpipe = nn_alloc (sizeof (struct nn_closed_pipe), "closed pipe (xserver)");
    cpipe->key = data->outitem.key;
    nn_queue_item_init(&cpipe->item);
    nn_queue_push(&xserver->cpipes, &cpipe->item);

    nn_free (data);
}

void nn_xserver_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xserver *xserver;
    struct nn_xserver_data *data;

    xserver = nn_cont (self, struct nn_xserver, sockbase);
    data = nn_pipe_getdata (pipe);

    nn_fq_in (&xserver->inpipes, &data->initem);
}

void nn_xserver_out (NN_UNUSED struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xserver_data *data;

    data = nn_pipe_getdata (pipe);
    data->flags |= NN_XSERVER_OUT;
}

int nn_xserver_events (struct nn_sockbase *self)
{
    return (nn_fq_can_recv (&nn_cont (self, struct nn_xserver,
        sockbase)->inpipes) ? NN_SOCKBASE_EVENT_IN : 0) | NN_SOCKBASE_EVENT_OUT;
}

int nn_xserver_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    uint32_t key;
    struct nn_xserver *xserver;
    struct nn_xserver_data *data;

    xserver = nn_cont (self, struct nn_xserver, sockbase);

    /*  We treat invalid peer ID as if the peer was non-existent. */
    if (nn_slow (nn_chunkref_size (&msg->sphdr) < sizeof (uint32_t))) {
        nn_msg_term (msg);
        return 0;
    }

    /*  Retrieve the destination peer ID. Trim it from the header. */
    key = nn_getl (nn_chunkref_data (&msg->sphdr));
    nn_chunkref_trim (&msg->sphdr, 4);

    /*  Find the appropriate pipe to send the message to. If there's none,
        or if it's not ready for sending, silently drop the message. */
    data = nn_cont (nn_hash_get (&xserver->outpipes, key), struct nn_xserver_data,
        outitem);
    if (!data) {
        nn_msg_term (msg);
        return -ENOTCONN;
    }
    if (!nn_chunkref_size (&msg->body)) {
        rc = nn_pipe_close (data->pipe);
        errnum_assert (rc >= 0, -rc);
        return 0;
    }
    if (!(data->flags & NN_XSERVER_OUT)) {
        nn_msg_term (msg);
        return 0;
    }

    /*  Send the message. */
    rc = nn_pipe_send (data->pipe, msg);
    errnum_assert (rc >= 0, -rc);
    if (rc & NN_PIPE_RELEASE)
        data->flags &= ~NN_XSERVER_OUT;

    return 0;
}

int nn_xserver_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_xserver *xserver;
    struct nn_pipe *pipe;
    struct nn_chunkref ref;
    struct nn_xserver_data *pipedata;
    struct nn_queue_item *item;
    struct nn_closed_pipe *cpipe;
    size_t off;

    xserver = nn_cont (self, struct nn_xserver, sockbase);

    if ((item = nn_queue_pop (&xserver->cpipes))) {
        cpipe = nn_cont (item, struct nn_closed_pipe, item);
        nn_msg_init (msg, 0);
        nn_chunkref_init (&msg->sphdr, sizeof (uint32_t));
        nn_putl (nn_chunkref_data (&msg->sphdr), cpipe->key);
        nn_free (cpipe);
        return 0;
    }

    rc = nn_fq_recv (&xserver->inpipes, msg, &pipe);
    if (nn_slow (rc < 0))
        return rc;

    /*  Prepend the header by the pipe key. */
    pipedata = nn_pipe_getdata (pipe);
    nn_chunkref_init (&ref,
        nn_chunkref_size (&msg->sphdr) +
        (pipedata->peername_sent ? sizeof (uint32_t) : NN_CHUNKREF_MAX));
    nn_putl (nn_chunkref_data (&ref), pipedata->outitem.key);
    off = sizeof (uint32_t);
    if (!pipedata->peername_sent) {
        nn_pipe_getpeername (pipe, 
            (char *) nn_chunkref_data (&ref) + sizeof (uint32_t),
            NN_CHUNKREF_MAX - sizeof (uint32_t));
        pipedata->peername_sent = 1;
        off = NN_CHUNKREF_MAX;
    }
    memcpy (((uint8_t*) nn_chunkref_data (&ref)) + off,
        nn_chunkref_data (&msg->sphdr), nn_chunkref_size (&msg->sphdr));
    nn_chunkref_term (&msg->sphdr);
    nn_chunkref_mv (&msg->sphdr, &ref);

    return 0;
}

int nn_xserver_create (void *hint, struct nn_sockbase **sockbase)
{
    struct nn_xserver *self;

    self = nn_alloc (sizeof (struct nn_xserver), "socket (xserver)");
    alloc_assert (self);
    nn_xserver_init (self, &nn_xserver_sockbase_vfptr, hint);
    *sockbase = &self->sockbase;

    return 0;
}

int nn_xserver_ispeer (int socktype)
{
    return (socktype == NN_CLIENT || socktype == NN_PAIR) ? 1 : 0;
}

struct nn_socktype nn_xserver_socktype = {
    AF_SP_RAW,
    NN_SERVER,
    0,
    nn_xserver_create,
    nn_xserver_ispeer,
};
