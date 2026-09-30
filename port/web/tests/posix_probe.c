/* Exercise the actual exported guest/host boundary, including Winsock errors,
   sockaddr layout and opt-out fallback. No Halo assets or engine are needed. */
#include "../posix_bridge.h"
#include "posix.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static int emit(void *context, uint32_t peer, int reliable, const void *bytes, size_t size) {
    (void)context;
    (void)peer;
    (void)reliable;
    (void)bytes;
    (void)size;
    return 1;
}
static struct sockaddr_in address(uint32_t ip, uint16_t port) {
    struct sockaddr_in out = {0};
    uint16_t family = AF_INET;
    memcpy(&out, &family, 2);
    out.sin_addr.s_addr = htonl(ip);
    out.sin_port = htons(port);
    return out;
}
int main(void) {
    int real = posix_socket(AF_INET, SOCK_DGRAM, 0);
    assert(real >= 0);
    unsigned char id[6] = {2, 0, 0, 0, 0, 1};
    struct hw_net *net = hw_net_create(id, emit, NULL);
    assert(net);
    assert(!halo_web_install_transport(net));
    int listen = posix_socket(AF_INET, SOCK_STREAM, 0),
        client = posix_socket(AF_INET, SOCK_STREAM, 0);
    assert(hw_owns_socket(net, listen) && !hw_owns_socket(net, real));
    assert(halo_web_install_transport(NULL) < 0 && errno == EBUSY);
    struct sockaddr_in host = address(0, 5150), local = address(0x7f000001, 5150);
    assert(!posix_socket_bind(listen, &host, sizeof(host)) && !posix_socket_listen(listen, 8));
    assert(posix_socket_accept(listen, NULL, NULL) < 0 && posix_socket_last_error() == 10035);
    assert(!posix_socket_set_nonblocking(client, 1));
    assert(posix_socket_set_nonblocking(client, 0) < 0 && posix_socket_last_error() == 10045);
    assert(!posix_socket_connect(client, &local, sizeof(local)));
    struct sockaddr_in peer;
    int length = sizeof(peer), accepted = posix_socket_accept(listen, &peer, &length);
    assert(accepted >= 0);
    uint16_t family;
    memcpy(&family, &peer, 2);
    assert(family == AF_INET && ntohl(peer.sin_addr.s_addr) == 0x7f000001 &&
           ntohs(peer.sin_port) >= 40000);
    assert(posix_socket_send(client, "guest ABI", 9, 0) == 9 && !posix_socket_last_error());
    posix_ulong available = 0;
    assert(!posix_socket_bytes_available(accepted, &available) && available == 9);
    int read[] = {accepted}, read_count = 1;
    assert(posix_socket_select(read, &read_count, NULL, NULL, NULL, NULL, 0, 0, 0) == 1 &&
           read_count == 1);
    char bytes[32];
    assert(posix_socket_recv(accepted, bytes, sizeof(bytes), MSG_PEEK) == 9 &&
           !memcmp(bytes, "guest ABI", 9));
    assert(posix_socket_recv(accepted, bytes, sizeof(bytes), 0) == 9);
    assert(posix_socket_send(client, bytes, -1, 0) < 0 && posix_socket_last_error() == 10022);
    int type = 0;
    length = sizeof(type);
    assert(!posix_socket_getsockopt(accepted, 0xffff, 0x1008, &type, &length) &&
           type == SOCK_STREAM);
    assert(!posix_socket_close(client) &&
           posix_socket_recv(accepted, bytes, sizeof(bytes), 0) == 0);
    assert(!posix_socket_close(accepted) && !posix_socket_close(listen));
    int udp = posix_socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in endpoint = address(0, 5151), target = address(0x7f000001, 5151);
    assert(!posix_socket_bind(udp, &endpoint, sizeof(endpoint)));
    assert(posix_socket_sendto(udp, "input", 5, 0, &target, sizeof(target)) == 5);
    length = sizeof(peer);
    assert(posix_socket_recvfrom(udp, bytes, sizeof(bytes), 0, &peer, &length) == 5 &&
           !memcmp(bytes, "input", 5));
    memcpy(&family, &peer, 2);
    assert(family == AF_INET && ntohs(peer.sin_port) == 5151);
    assert(!posix_socket_close(udp) && !posix_socket_close(real));
    assert(!halo_web_install_transport(NULL));
    hw_net_destroy(net);
    real = posix_socket(AF_INET, SOCK_DGRAM, 0);
    assert(real >= 0 && !posix_socket_close(real));
    puts("PASS: actual iOS Winsock boundary, sockaddr byte order, readiness, errors, datagrams, "
         "guarded detach and BSD fallback");
    return 0;
}
