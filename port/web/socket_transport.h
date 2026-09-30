#ifndef HALO_WEB_SOCKET_TRANSPORT_H
#define HALO_WEB_SOCKET_TRANSPORT_H
#include <stddef.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/socket.h>
#ifdef __cplusplus
extern "C" {
#endif
struct hw_net;
/* IPv4 values and ports in this API use host byte order. emit: 1 accepted,
   0 backpressure, -1 disconnected. The callback runs without the socket lock. */
typedef int (*hw_emit)(void *, uint32_t peer, int reliable, const void *, size_t);
struct hw_net *hw_net_create(const unsigned char identifier[6], hw_emit emit, void *context);
void hw_net_destroy(struct hw_net *net);
uint32_t hw_net_add_peer(struct hw_net *net, const unsigned char identifier[6]);
void hw_net_peer_state(struct hw_net *net, uint32_t peer, int reliable, int unreliable,
                       int writable);
void hw_net_remove_peer(struct hw_net *net, uint32_t peer);
int hw_net_peer_ready(struct hw_net *net, uint32_t peer);
/* 1 consumed (including UDP drop), 0 retry reliable delivery, -1 malformed. */
int hw_net_receive(struct hw_net *net, uint32_t peer, const void *frame, size_t size);

/* Native descriptors are readiness handles. Game bytes live in bounded queues;
   no third-party DataChannel callback calls the Halo engine. */
int hw_socket(struct hw_net *net, int type);
int hw_owns_socket(struct hw_net *net, int fd);
int hw_socket_count(struct hw_net *net);
int hw_close(struct hw_net *net, int fd);
int hw_bind(struct hw_net *net, int fd, uint32_t ip, uint16_t port);
int hw_listen(struct hw_net *net, int fd, int backlog);
int hw_connect(struct hw_net *net, int fd, uint32_t ip, uint16_t port);
int hw_accept(struct hw_net *net, int fd, uint32_t *ip, uint16_t *port);
int hw_send(struct hw_net *net, int fd, const void *bytes, size_t size, int flags);
int hw_sendto(struct hw_net *net, int fd, const void *bytes, size_t size, int flags, uint32_t ip,
              uint16_t port);
int hw_recv(struct hw_net *net, int fd, void *bytes, size_t size, int flags);
int hw_recvfrom(struct hw_net *net, int fd, void *bytes, size_t size, int flags, uint32_t *ip,
                uint16_t *port);
int hw_shutdown(struct hw_net *net, int fd, int how);
int hw_nonblocking(struct hw_net *net, int fd, int enabled);
int hw_available(struct hw_net *net, int fd, int *bytes);
int hw_name(struct hw_net *net, int fd, int remote, uint32_t *ip, uint16_t *port);
int hw_option(struct hw_net *net, int fd, int option, int *value);
int hw_select(struct hw_net *net, int limit, fd_set *read, fd_set *write, fd_set *error,
              struct timeval *timeout);
#ifdef __cplusplus
}
#endif
#endif
