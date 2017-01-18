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

#include "server.h"
#include "xserver.h"

#include "../../nn.h"
#include "../../clntsrv.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"
#include "../../utils/chunkref.h"
#include "../../utils/wire.h"

#include <stddef.h>
#include <string.h>

static const struct nn_sockbase_vfptr nn_server_sockbase_vfptr = {
    NULL,
    nn_server_destroy,
    nn_xserver_add,
    nn_xserver_rm,
    nn_xserver_in,
    nn_xserver_out,
    nn_server_events,
    nn_server_send,
    nn_server_recv,
    NULL,
    NULL
};

void nn_server_init (struct nn_server *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint)
{
    nn_xserver_init (&self->xserver, vfptr, hint);
    self->flags = 0;
}

void nn_server_term (struct nn_server *self)
{
    nn_xserver_term (&self->xserver);
}

void nn_server_destroy (struct nn_sockbase *self)
{
    struct nn_server *server;

    server = nn_cont (self, struct nn_server, xserver.sockbase);

    nn_server_term (server);
    nn_free (server);
}

int nn_server_events (struct nn_sockbase *self)
{
    struct nn_server *server;
    int events;

    server = nn_cont (self, struct nn_server, xserver.sockbase);
    events = nn_xserver_events (&server->xserver.sockbase);
    /*if (!(server->flags & NN_SERVER_INPROGRESS))
        events &= ~NN_SOCKBASE_EVENT_OUT;*/
    return events;
}

int nn_server_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_server *server;

    server = nn_cont (self, struct nn_server, xserver.sockbase);

    /*  Send the reply. If it cannot be sent because of pushback,
        drop it silently. */
    rc = nn_xserver_send (&server->xserver.sockbase, msg);
    errnum_assert (rc == 0 || rc == -EAGAIN, -rc);

    return 0;
}

int nn_server_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_server *server;

    server = nn_cont (self, struct nn_server, xserver.sockbase);

    /*  Receive the request. */
    rc = nn_xserver_recv (&server->xserver.sockbase, msg);
    if (nn_slow (rc == -EAGAIN))
        return -EAGAIN;
    errnum_assert (rc == 0, -rc);

    return 0;
}

static int nn_server_create (void *hint, struct nn_sockbase **sockbase)
{
    struct nn_server *self;

    self = nn_alloc (sizeof (struct nn_server), "socket (server)");
    alloc_assert (self);
    nn_server_init (self, &nn_server_sockbase_vfptr, hint);
    *sockbase = &self->xserver.sockbase;

    return 0;
}

struct nn_socktype nn_server_socktype = {
    AF_SP,
    NN_SERVER,
    0,
    nn_server_create,
    nn_xserver_ispeer,
};
