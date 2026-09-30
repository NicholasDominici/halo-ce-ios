#include "../socket_transport.h"
#include "../frames.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
struct link {
    struct hw_net *other;
    uint32_t source;
    int blocked;
};
static int emit(void *context, uint32_t ip, int reliable, const void *bytes, size_t size) {
    (void)ip;
    struct link *link = context;
    struct hw_frame frame;
    assert(!hw_frame_decode(bytes, size, &frame));
    assert(reliable == (frame.type != HWF_DATAGRAM));
    return link->blocked ? 0 : hw_net_receive(link->other, link->source, bytes, size);
}
static int readable(struct hw_net *n, int fd) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    struct timeval zero = {0};
    return hw_select(n, fd + 1, &set, NULL, NULL, &zero) == 1 && FD_ISSET(fd, &set);
}
int main(void) {
    unsigned char aid[6] = {2, 0, 0, 0, 0, 1}, bid[6] = {2, 0, 0, 0, 0, 2};
    struct link ab = {0}, ba = {0};
    struct hw_net *a = hw_net_create(aid, emit, &ab), *b = hw_net_create(bid, emit, &ba);
    assert(a && b);
    uint32_t bip = hw_net_add_peer(a, bid), aip = hw_net_add_peer(b, aid);
    assert(aip && bip);
    ab.other = b;
    ab.source = aip;
    ba.other = a;
    ba.source = bip;
    hw_net_peer_state(a, bip, 1, 1, 1);
    hw_net_peer_state(b, aip, 1, 1, 1);
    assert(hw_net_peer_ready(a, bip));
    assert(!hw_net_add_peer(a, aid));
    assert(hw_net_add_peer(a, bid) == bip);

    /* An independent golden fixture, not just encode/decode roundtripping. */
    const unsigned char golden[] = {0x48, 1, 2, 0, 0x12, 0x34, 0x56, 0x78, 0x14, 0x1f, 0x14, 0x1e};
    unsigned char encoded[16396];
    struct hw_frame f = {HWF_OPEN, 0x12345678, 5151, 5150, NULL, 0};
    assert(hw_frame_encode(encoded, sizeof(encoded), &f) == 12 && !memcmp(golden, encoded, 12));
    struct hw_frame decoded;
    assert(!hw_frame_decode(golden, sizeof(golden), &decoded) && decoded.stream == f.stream);
    encoded[3] = 1;
    assert(hw_frame_decode(encoded, 12, &decoded) < 0);

    int listen = hw_socket(b, SOCK_STREAM), client = hw_socket(a, SOCK_STREAM);
    assert(listen >= 0 && client >= 0);
    assert(!hw_bind(b, listen, 0, 5150) && !hw_listen(b, listen, 8));
    assert(!hw_connect(a, client, bip, 5150));
    assert(readable(b, listen));
    uint32_t ip;
    uint16_t port;
    int server = hw_accept(b, listen, &ip, &port);
    assert(server >= 0 && ip == aip && port >= 40000);
    assert(!readable(b, listen));
    char data[65536];
    assert(hw_send(a, client, "Halo handshake", 14, 0) == 14 && readable(b, server));
    assert(hw_recv(b, server, data, 4, MSG_PEEK) == 4 && !memcmp(data, "Halo", 4));
    assert(hw_recv(b, server, data, 4, 0) == 4 && readable(b, server));
    assert(hw_recv(b, server, data, sizeof(data), 0) == 10 && !memcmp(data, " handshake", 10));
    assert(!readable(b, server));
    assert(hw_send(b, server, "reply", 5, 0) == 5 &&
           hw_recv(a, client, data, sizeof(data), 0) == 5 && !memcmp(data, "reply", 5));
    memset(data, 0xa5, sizeof(data));
    for (int i = 0; i < 4; i++)
        assert(hw_send(a, client, data, 16384, 0) == 16384);
    assert(hw_send(a, client, data, 1, 0) < 0 && errno == EAGAIN);
    assert(hw_recv(b, server, data, sizeof(data), 0) == 65536);
    assert(hw_send(a, client, "retry", 5, 0) == 5 &&
           hw_recv(b, server, data, sizeof(data), 0) == 5);
    ab.blocked = 1;
    assert(hw_send(a, client, data, 1, 0) < 0 && errno == EAGAIN);
    ab.blocked = 0;
    assert(!hw_close(a, client));
    assert(readable(b, server) && hw_recv(b, server, data, sizeof(data), 0) == 0);

    int udp_a = hw_socket(a, SOCK_DGRAM), udp_b = hw_socket(b, SOCK_DGRAM);
    assert(!hw_bind(a, udp_a, 0, 5151) && !hw_bind(b, udp_b, 0, 5151));
    assert(hw_sendto(a, udp_a, "action", 6, 0, bip, 5151) == 6 && readable(b, udp_b));
    assert(hw_recvfrom(b, udp_b, data, 3, 0, &ip, &port) == 3 && !memcmp(data, "act", 3) &&
           ip == aip && port == 5151);
    assert(!readable(b, udp_b));
    assert(!hw_connect(b, udp_b, aip, 5151));
    assert(hw_send(b, udp_b, "response", 8, 0) == 8 &&
           hw_recv(a, udp_a, data, sizeof(data), 0) == 8);
    int duplicate = hw_socket(b, SOCK_DGRAM);
    assert(hw_bind(b, duplicate, 0, 5151) < 0 && errno == EADDRINUSE);
    assert(!hw_close(b, duplicate));
    int different = hw_socket(a, SOCK_DGRAM);
    assert(!hw_bind(a, different, 0, 5152));
    assert(hw_sendto(a, different, "filtered", 8, 0, bip, 5151) == 8 && !readable(b, udp_b));
    assert(!hw_close(a, different));
    assert(hw_sendto(a, udp_a, data, 1501, 0, bip, 5151) < 0 && errno == EMSGSIZE);
    assert(hw_sendto(a, udp_a, NULL, 0, 0, bip, 5151) == 0 && readable(b, udp_b));
    assert(hw_recv(b, udp_b, data, sizeof(data), 0) == 0 && !readable(b, udp_b));

    /* Local host/client sockets still function without a DataChannel. */
    int local = hw_socket(b, SOCK_STREAM);
    assert(!hw_connect(b, local, 0x7f000001, 5150));
    int accepted = hw_accept(b, listen, &ip, &port);
    assert(accepted >= 0 && ip == 0x7f000001);
    assert(hw_send(b, local, "self", 4, 0) == 4 &&
           hw_recv(b, accepted, data, sizeof(data), 0) == 4);
    assert(!hw_close(b, local) && hw_recv(b, accepted, data, sizeof(data), 0) == 0);
    assert(hw_recv(b, accepted, NULL, 0, 0) == 0);
    /* Closing a listener must also close unaccepted remote and local streams. */
    int waiting_local = hw_socket(b, SOCK_STREAM), waiting_remote = hw_socket(a, SOCK_STREAM);
    assert(!hw_connect(b, waiting_local, 0x7f000001, 5150) &&
           !hw_connect(a, waiting_remote, bip, 5150));
    assert(!hw_close(b, listen));
    assert(hw_recv(b, waiting_local, data, sizeof(data), 0) == 0 &&
           hw_recv(a, waiting_remote, data, sizeof(data), 0) == 0);
    assert(!hw_close(b, waiting_local) && !hw_close(a, waiting_remote));

    int pipe_fds[2];
    assert(!pipe(pipe_fds));
    assert(write(pipe_fds[1], "x", 1) == 1);
    fd_set set;
    FD_ZERO(&set);
    FD_SET(pipe_fds[0], &set);
    struct timeval zero = {0};
    assert(hw_select(b, pipe_fds[0] + 1, &set, NULL, NULL, &zero) == 1 &&
           FD_ISSET(pipe_fds[0], &set));
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    hw_net_remove_peer(b, aip);
    int error;
    assert(!hw_option(b, udp_b, SO_ERROR, &error) && error == ECONNRESET);
    hw_net_destroy(a);
    hw_net_destroy(b);
    puts("PASS: native frame fixtures, stream/datagram delivery, partial reads, bounded "
         "backpressure, EOF, local sockets, peer removal and mixed select");
    return 0;
}
