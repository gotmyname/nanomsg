/*
    Copyright (c) 2012-2013 Martin Sustrik  All rights reserved.
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

#include "xclient.h"

#include "../../nn.h"
#include "../../clntsrv.h"

#include "../utils/excl.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"
#include "../../utils/attr.h"

struct nn_xclient {
    struct nn_sockbase sockbase;
    struct nn_excl excl;
};

/*  Private functions. */
static void nn_xclient_init (struct nn_xclient *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint);
static void nn_xclient_term (struct nn_xclient *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_xclient_destroy (struct nn_sockbase *self);
static int nn_xclient_add (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xclient_rm (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xclient_in (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xclient_out (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_xclient_events (struct nn_sockbase *self);
static int nn_xclient_send (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xclient_recv (struct nn_sockbase *self, struct nn_msg *msg);
static const struct nn_sockbase_vfptr nn_xclient_sockbase_vfptr = {
    NULL,
    nn_xclient_destroy,
    nn_xclient_add,
    nn_xclient_rm,
    nn_xclient_in,
    nn_xclient_out,
    nn_xclient_events,
    nn_xclient_send,
    nn_xclient_recv,
    NULL,
    NULL
};

static void nn_xclient_init (struct nn_xclient *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint)
{
    nn_sockbase_init (&self->sockbase, vfptr, hint);
    nn_excl_init (&self->excl);
}

static void nn_xclient_term (struct nn_xclient *self)
{
    nn_excl_term (&self->excl);
    nn_sockbase_term (&self->sockbase);
}

void nn_xclient_destroy (struct nn_sockbase *self)
{
    struct nn_xclient *xclient;

    xclient = nn_cont (self, struct nn_xclient, sockbase);

    nn_xclient_term (xclient);
    nn_free (xclient);
}

static int nn_xclient_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    return nn_excl_add (&nn_cont (self, struct nn_xclient, sockbase)->excl,
        pipe);
}

static void nn_xclient_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    nn_excl_rm (&nn_cont (self, struct nn_xclient, sockbase)->excl, pipe);
}

static void nn_xclient_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    nn_excl_in (&nn_cont (self, struct nn_xclient, sockbase)->excl, pipe);
}

static void nn_xclient_out (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    nn_excl_out (&nn_cont (self, struct nn_xclient, sockbase)->excl, pipe);
}

static int nn_xclient_events (struct nn_sockbase *self)
{
    struct nn_xclient *xclient;
    int events;

    xclient = nn_cont (self, struct nn_xclient, sockbase);

    events = 0;
    if (nn_excl_can_recv (&xclient->excl))
        events |= NN_SOCKBASE_EVENT_IN;
    if (nn_excl_can_send (&xclient->excl))
        events |= NN_SOCKBASE_EVENT_OUT;
    return events;
}

static int nn_xclient_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    return nn_excl_send (&nn_cont (self, struct nn_xclient, sockbase)->excl,
        msg);
}

static int nn_xclient_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;

    rc = nn_excl_recv (&nn_cont (self, struct nn_xclient, sockbase)->excl, msg);

    /*  Discard NN_PIPEBASE_PARSED flag. */
    return rc < 0 ? rc : 0;
}

int nn_xclient_create (void *hint, struct nn_sockbase **sockbase)
{
    struct nn_xclient *self;

    self = nn_alloc (sizeof (struct nn_xclient), "socket (client)");
    alloc_assert (self);
    nn_xclient_init (self, &nn_xclient_sockbase_vfptr, hint);
    *sockbase = &self->sockbase;

    return 0;
}

int nn_xclient_ispeer (int socktype)
{
    return socktype == NN_SERVER ? 1 : 0;
}

struct nn_socktype nn_xclient_socktype = {
    AF_SP_RAW,
    NN_CLIENT,
    0,
    nn_xclient_create,
    nn_xclient_ispeer,
};
