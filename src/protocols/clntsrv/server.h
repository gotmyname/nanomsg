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

#ifndef NN_SERVER_INCLUDED
#define NN_SERVER_INCLUDED

#include "../../protocol.h"
#include "xserver.h"

struct nn_server {
    struct nn_xserver xserver;
    uint32_t flags;
};

/*  Some users may want to extend the SERVER protocol similar to how SERVER extends XSERVER.
    Expose these methods to improve extensibility. */
void nn_server_init (struct nn_server *self,
const struct nn_sockbase_vfptr *vfptr, void *hint);
void nn_server_term (struct nn_server *self);

/*  Implementation of nn_sockbase's virtual functions. */
void nn_server_destroy (struct nn_sockbase *self);
int nn_server_events (struct nn_sockbase *self);
int nn_server_send (struct nn_sockbase *self, struct nn_msg *msg);
int nn_server_recv (struct nn_sockbase *self, struct nn_msg *msg);

#endif
