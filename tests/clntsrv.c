/*
    Copyright (c) 2012 Martin Sustrik  All rights reserved.

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

#include "../src/nn.h"
#include "../src/pair.h"
#include "../src/clntsrv.h"

#include "testutil.h"

#ifndef NN_CHUNKREF_MAX
  #define NN_CHUNKREF_MAX 32
#endif

#define SOCKET_ADDRESS "inproc://test"
#define EXPECTED_PEERNAME "inproc"

//#define SOCKET_ADDRESS "ipc://test"
//#define EXPECTED_PEERNAME "ipc"

//#define SOCKET_ADDRESS "tcp://127.0.0.1:3344"
//#define EXPECTED_PEERNAME "tcp://127.0.0.1"

//#define SOCKET_ADDRESS "ws://127.0.0.1:3344"
//#define EXPECTED_PEERNAME "ws://127.0.0.1"

static void test_sendmsg_impl (char *file, int line, int sock, char *data,
                               uint32_t key);
static void test_recvmsg_impl (char *file, int line, int sock, char *data,
                               uint32_t *key, char *peername);

#define test_sendmsg(s, d, k) test_sendmsg_impl (__FILE__, __LINE__, (s), (d), (k))
#define test_recvmsg(s, d, k) test_recvmsg_impl (__FILE__, __LINE__, (s), (d), (k), NULL)
#define test_recvmsg1(s, d, k, p) test_recvmsg_impl (__FILE__, __LINE__, (s), (d), (k), (p))

static void NN_UNUSED test_sendmsg_impl (char *file, int line,
    int sock, char *data, uint32_t key)
{
    struct nn_iovec iov;
    struct nn_msghdr hdr;
    size_t data_len;
    char control[256];
    struct nn_cmsghdr *chdr;
    size_t spsz, sptotalsz;
    char *ptr;
    int rc;

    data_len = strlen (data);

    chdr = (struct nn_cmsghdr *)control;
    ptr = (char *)chdr;
    spsz = sizeof(key);
    sptotalsz = NN_CMSG_SPACE (spsz+sizeof (size_t));
    chdr->cmsg_len = sptotalsz;
    chdr->cmsg_level = PROTO_SP;
    chdr->cmsg_type = SP_HDR;
    ptr += sizeof (*chdr);
    *(size_t *)(void *)ptr = spsz;
    ptr += sizeof (size_t);
    memcpy(ptr, &key, sizeof(key));

    iov.iov_base = (void*) data;
    iov.iov_len = data_len;

    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    hdr.msg_control = control;
    hdr.msg_controllen = sptotalsz;

    rc = nn_sendmsg (sock, &hdr, 0);
    if (rc < 0) {
        fprintf (stderr, "Failed to sendmsg: %s [%d] (%s:%d)\n",
            nn_err_strerror (errno),
            (int) errno, file, line);
        nn_err_abort ();
    }
    if (rc != (int)data_len) {
        fprintf (stderr, "Data to send is truncated: %d != %d (%s:%d)\n",
            rc, (int) data_len,
            file, line);
        nn_err_abort ();
    }
}

static int NN_UNUSED peername_eq (char *peername, char *expected)
{
    int len = strlen(expected) + 1;
    if (!memcmp (expected, "tcp", 3) || !memcmp (expected, "ws", 2)) {
        len--;
    }
    return memcmp(peername, expected, len) == 0;
}

static void NN_UNUSED test_recvmsg_impl (char *file, int line,
    int sock, char *data, uint32_t *key, char *peername)
{
    struct nn_iovec iov;
    struct nn_msghdr hdr;
    size_t data_len;
    int rc;
    char *buf;
    char control[256];
    struct nn_cmsghdr *chdr;
    size_t spsz, sptotalsz;
    char *ptr;

    data_len = strlen (data);
    /*  We allocate plus one byte so that we are sure that message received
        has correct length and not truncated  */
    buf = malloc (data_len+1);
    alloc_assert (buf);

    iov.iov_base = buf;
    iov.iov_len = data_len;

    chdr = (struct nn_cmsghdr *)control;
    ptr = (char *)chdr;
    spsz = peername ? NN_CHUNKREF_MAX : sizeof(*key);
    sptotalsz = NN_CMSG_SPACE (spsz+sizeof (size_t));
    
    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    hdr.msg_control = control;
    hdr.msg_controllen = sizeof(control);

    rc = nn_recvmsg (sock, &hdr, 0);
    if (rc < 0) {
        fprintf (stderr, "Failed to recvmsg: %s [%d] (%s:%d)\n",
            nn_err_strerror (errno),
            (int) errno, file, line);
        nn_err_abort ();
    }
    if (rc != (int)data_len) {
        fprintf (stderr, "Received data has wrong length: %d != %d (%s:%d)\n",
            rc, (int) data_len,
            file, line);
        nn_err_abort ();
    }
    if (memcmp (data, buf, data_len) != 0) {
        /*  We don't print the data as it may have binary garbage  */
        fprintf (stderr, "Received data is wrong (%s:%d)\n", file, line);
        nn_err_abort ();
    }
    if (chdr->cmsg_len != sptotalsz) {
        fprintf (stderr, "Received cmsghdr len is wrong %d != %d (%s:%d)\n",
            (int)chdr->cmsg_len, (int)sptotalsz, file, line);
        nn_err_abort ();
    }
    if (chdr->cmsg_level != PROTO_SP || chdr->cmsg_type != SP_HDR) {
        fprintf (stderr, "Received cmsghdr is wrong (%s:%d)\n", file, line);
        nn_err_abort ();
    }
    ptr += sizeof (*chdr);
    if (*(size_t *)(void *)ptr != spsz) {
        fprintf (stderr, "Received cmsghdr data len is wrong %d != %d (%s:%d)\n",
            (int)*(size_t *)(void *)ptr, (int)spsz, file, line);
        nn_err_abort ();
    }
    ptr += sizeof (size_t);
    
    memcpy(key, ptr, sizeof(*key));
    if (peername && !peername_eq (ptr + sizeof(*key), peername)) {
        fprintf (stderr, "Received peername is wrong (%s:%d)\n", 
            file, line);
        nn_err_abort ();
    }

    free (buf);
}

int main ()
{
    int srv1;
    int clnt1;
    int clnt2;
    int timeo;
    uint32_t key1;
    uint32_t key2;

    /*  Test clnt/srv with full socket types. */
    srv1 = test_socket (AF_SP, NN_SERVER);
    test_bind (srv1, SOCKET_ADDRESS);
    clnt1 = test_socket (AF_SP, NN_CLIENT);
    test_connect (clnt1, SOCKET_ADDRESS);
    clnt2 = test_socket (AF_SP, NN_CLIENT);
    test_connect (clnt2, SOCKET_ADDRESS);

    /*  Check fair queueing the requests. */
    test_send (clnt2, "ABC2");
    test_recvmsg1 (srv1, "ABC2", &key2, EXPECTED_PEERNAME);
    test_sendmsg (srv1, "ABC2", key2);
    test_recv (clnt2, "ABC2");

    test_send (clnt1, "ABC1");
    test_recvmsg1 (srv1, "ABC1", &key1, EXPECTED_PEERNAME);
    test_sendmsg (srv1, "ABC1", key1);
    test_recv (clnt1, "ABC1");

    /*  Check interleaved requests. */
    test_send (clnt2, "ABC2");
    test_recvmsg (srv1, "ABC2", &key2);
    test_send (clnt1, "ABC1");
    test_recvmsg (srv1, "ABC1", &key1);

    test_sendmsg (srv1, "ABC2", key2);
    test_recv (clnt2, "ABC2");
    test_sendmsg (srv1, "ABC1", key1);
    test_recv (clnt1, "ABC1");
    
    test_close (srv1);
    test_close (clnt1);
    test_close (clnt2);

    /*  Test use pair as client. */
    srv1 = test_socket (AF_SP, NN_SERVER);
    test_bind (srv1, SOCKET_ADDRESS);
    clnt1 = test_socket (AF_SP, NN_PAIR);
    test_connect (clnt1, SOCKET_ADDRESS);

    test_send (clnt1, "ABC");
    test_recvmsg1 (srv1, "ABC", &key1, EXPECTED_PEERNAME);
    test_sendmsg (srv1, "ABC", key1);
    test_recv (clnt1, "ABC");

    test_close (srv1);
    test_close (clnt1);

    /*  Test request pipeline  */
    srv1 = test_socket (AF_SP, NN_SERVER);
    test_bind (srv1, SOCKET_ADDRESS);
    clnt1 = test_socket (AF_SP, NN_CLIENT);
    test_connect (clnt1, SOCKET_ADDRESS);

    test_send (clnt1, "ABC");
    test_send (clnt1, "DEF");

    timeo = 100;
    test_recvmsg1 (srv1, "ABC", &key1, EXPECTED_PEERNAME);
    test_recvmsg (srv1, "DEF", &key2);
    if (key1 != key2) {
        fprintf (stderr, "Received keys unmatch: %u != %u (%s:%d)\n",
            key1, key2, __FILE__, __LINE__);
        nn_err_abort ();
    }

    test_close (clnt1);
    test_close (srv1);

    return 0;
}

