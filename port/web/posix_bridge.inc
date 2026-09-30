/* Included after the Darwin-adapted BSD implementation. Keep its Winsock error
   translation and per-thread last_error. The release build does not include
   this experimental override unless HALO_ENABLE_WEBRTC is set. */
#include <stdatomic.h>
#include <netinet/tcp.h>
static _Atomic(struct hw_net *) web_net;
int halo_web_install_transport(struct hw_net *net) {
    struct hw_net *old = atomic_load(&web_net);
    if (old == net)
        return 0;
    if (old && hw_socket_count(old)) {
        errno = EBUSY;
        return -1;
    }
    atomic_store(&web_net, net);
    return 0;
}
static struct hw_net *web_owner(int fd) {
    struct hw_net *net = atomic_load(&web_net);
    return net && hw_owns_socket(net, fd) ? net : NULL;
}
static int web_address(const void *input, int length, uint32_t *ip, uint16_t *port) {
    if (!input || length < (int)sizeof(struct sockaddr_in)) {
        errno = EINVAL;
        return -1;
    }
    uint16_t family;
    struct sockaddr_in address;
    memcpy(&family, input, 2);
    memcpy(&address, input, sizeof(address));
    if (family != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    *ip = ntohl(address.sin_addr.s_addr);
    *port = ntohs(address.sin_port);
    return 0;
}
static int web_output(void *output, int *length, uint32_t ip, uint16_t port) {
    if (!output)
        return 0;
    if (!length || *length < 0) {
        errno = EINVAL;
        return -1;
    }
    struct sockaddr_in address = {0};
    uint16_t family = AF_INET;
    address.sin_addr.s_addr = htonl(ip);
    address.sin_port = htons(port);
    memcpy(&address, &family, 2);
    int size = *length < (int)sizeof(address) ? *length : (int)sizeof(address);
    memcpy(output, &address, (size_t)size);
    *length = sizeof(address);
    return 0;
}
int posix_socket(int family, int type, int protocol) {
    struct hw_net *net = atomic_load(&web_net);
    if (!net || family != AF_INET)
        return bsd_posix_socket(family, type, protocol);
    if (protocol && protocol != (type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP)) {
        errno = EPROTONOSUPPORT;
        return fail();
    }
    return succeed(hw_socket(net, type));
}
int posix_socket_close(int fd) {
    struct hw_net *n = web_owner(fd);
    return n ? succeed(hw_close(n, fd)) : bsd_posix_socket_close(fd);
}
int posix_socket_bind(int fd, const void *address, int length) {
    struct hw_net *n = web_owner(fd);
    if (!n)
        return bsd_posix_socket_bind(fd, address, length);
    uint32_t ip;
    uint16_t port;
    if (web_address(address, length, &ip, &port))
        return fail();
    return succeed(hw_bind(n, fd, ip, port));
}
int posix_socket_connect(int fd, const void *address, int length) {
    struct hw_net *n = web_owner(fd);
    if (!n)
        return bsd_posix_socket_connect(fd, address, length);
    uint32_t ip;
    uint16_t port;
    if (web_address(address, length, &ip, &port))
        return fail();
    return succeed(hw_connect(n, fd, ip, port));
}
int posix_socket_listen(int fd, int backlog) {
    struct hw_net *n = web_owner(fd);
    return n ? succeed(hw_listen(n, fd, backlog)) : bsd_posix_socket_listen(fd, backlog);
}
int posix_socket_accept(int fd, void *address, int *length) {
    struct hw_net *n = web_owner(fd);
    if (!n)
        return bsd_posix_socket_accept(fd, address, length);
    if (address && (!length || *length < 0)) {
        errno = EINVAL;
        return fail();
    }
    uint32_t ip;
    uint16_t port;
    int result = hw_accept(n, fd, &ip, &port);
    if (result >= 0)
        web_output(address, length, ip, port);
    return succeed(result);
}
int posix_socket_send(int fd, const void *bytes, int size, int flags) {
    struct hw_net *n = web_owner(fd);
    if (!n)
        return bsd_posix_socket_send(fd, bytes, size, flags);
    if (size < 0) {
        errno = EINVAL;
        return fail();
    }
    return succeed(hw_send(n, fd, bytes, (size_t)size, flags));
}
int posix_socket_sendto(int fd, const void *bytes, int size, int flags, const void *address,
                        int length) {
    struct hw_net *n = web_owner(fd);
    if (!n)
        return bsd_posix_socket_sendto(fd, bytes, size, flags, address, length);
    if (size < 0) {
        errno = EINVAL;
        return fail();
    }
    uint32_t ip;
    uint16_t port;
    if (web_address(address, length, &ip, &port))
        return fail();
    return succeed(hw_sendto(n, fd, bytes, (size_t)size, flags, ip, port));
}
int posix_socket_recv(int fd, void *bytes, int size, int flags) {
    struct hw_net *n = web_owner(fd);
    if (!n)
        return bsd_posix_socket_recv(fd, bytes, size, flags);
    if (size < 0) {
        errno = EINVAL;
        return fail();
    }
    return succeed(hw_recv(n, fd, bytes, (size_t)size, flags));
}
int posix_socket_recvfrom(int fd, void *bytes, int size, int flags, void *address, int *length) {
    struct hw_net *n = web_owner(fd);
    if (!n)
        return bsd_posix_socket_recvfrom(fd, bytes, size, flags, address, length);
    if (size < 0 || (address && (!length || *length < 0))) {
        errno = EINVAL;
        return fail();
    }
    uint32_t ip = 0;
    uint16_t port = 0;
    int result = hw_recvfrom(n, fd, bytes, (size_t)size, flags, &ip, &port);
    if (result >= 0)
        web_output(address, length, ip, port);
    return succeed(result);
}
int posix_socket_shutdown(int fd, int how) {
    struct hw_net *n = web_owner(fd);
    return n ? succeed(hw_shutdown(n, fd, how)) : bsd_posix_socket_shutdown(fd, how);
}
int posix_socket_set_nonblocking(int fd, int enabled) {
    struct hw_net *n = web_owner(fd);
    return n ? succeed(hw_nonblocking(n, fd, enabled))
             : bsd_posix_socket_set_nonblocking(fd, enabled);
}
int posix_socket_bytes_available(int fd, posix_ulong *count) {
    struct hw_net *n = web_owner(fd);
    if (!n)
        return bsd_posix_socket_bytes_available(fd, count);
    if (!count) {
        errno = EINVAL;
        return fail();
    }
    int value = 0, result = hw_available(n, fd, &value);
    if (!result)
        *count = (posix_ulong)value;
    return succeed(result);
}
int posix_socket_getsockopt(int fd, int level, int option, void *value, int *length) {
    struct hw_net *n = web_owner(fd);
    if (!n)
        return bsd_posix_socket_getsockopt(fd, level, option, value, length);
    if (!value || !length || *length < (int)sizeof(int)) {
        errno = EINVAL;
        return fail();
    }
    int host_level, host_option;
    if (translate_option(level, option, &host_level, &host_option) || host_level != SOL_SOCKET) {
        errno = ENOPROTOOPT;
        return fail();
    }
    int integer = 0, result = hw_option(n, fd, host_option, &integer);
    if (!result) {
        memcpy(value, &integer, sizeof(integer));
        *length = sizeof(integer);
    }
    return succeed(result);
}
int posix_socket_setsockopt(int fd, int level, int option, const void *value, int length) {
    struct hw_net *n = web_owner(fd);
    if (!n)
        return bsd_posix_socket_setsockopt(fd, level, option, value, length);
    int host_level, host_option;
    /* Preserve the existing Xbox-only no-op options. Queue sizes are fixed;
       SCTP provides reliability. No option silently changes delivery policy. */
    if (translate_option(level, option, &host_level, &host_option))
        return succeed(0);
    if (!value ||
        length < (int)(host_option == SO_LINGER && host_level == SOL_SOCKET ? sizeof(struct linger)
                                                                            : sizeof(int))) {
        errno = EINVAL;
        return fail();
    }
    if ((host_level == SOL_SOCKET && (host_option == SO_REUSEADDR || host_option == SO_KEEPALIVE ||
                                      host_option == SO_BROADCAST || host_option == SO_SNDBUF ||
                                      host_option == SO_RCVBUF || host_option == SO_LINGER)) ||
        (host_level == IPPROTO_TCP && host_option == TCP_NODELAY))
        return succeed(0);
    errno = ENOPROTOOPT;
    return fail();
}
static int web_name(int fd, int remote, void *address, int *length) {
    struct hw_net *n = web_owner(fd);
    if (!n)
        return remote ? bsd_posix_socket_getpeername(fd, address, length)
                      : bsd_posix_socket_getsockname(fd, address, length);
    if (!address || !length || *length < 0) {
        errno = EINVAL;
        return fail();
    }
    uint32_t ip;
    uint16_t port;
    int result = hw_name(n, fd, remote, &ip, &port);
    if (!result)
        result = web_output(address, length, ip, port);
    return succeed(result);
}
int posix_socket_getsockname(int fd, void *address, int *length) {
    return web_name(fd, 0, address, length);
}
int posix_socket_getpeername(int fd, void *address, int *length) {
    return web_name(fd, 1, address, length);
}
posix_ulong posix_local_ipv4_address(void) {
    return atomic_load(&web_net) ? htonl(0x7f000001) : bsd_posix_local_ipv4_address();
}
int posix_socket_select(int *read, int *read_count, int *write, int *write_count, int *error,
                        int *error_count, posix_long seconds, posix_long micros, int infinite) {
    struct hw_net *n = atomic_load(&web_net);
    if (!n)
        return bsd_posix_socket_select(read, read_count, write, write_count, error, error_count,
                                       seconds, micros, infinite);
    if ((read && !read_count) || (write && !write_count) || (error && !error_count)) {
        errno = EINVAL;
        return fail();
    }
    fd_set r, w, e;
    int maximum = -1;
    maximum = fill_set(&r, read, read ? *read_count : 0, maximum);
    maximum = fill_set(&w, write, write ? *write_count : 0, maximum);
    maximum = fill_set(&e, error, error ? *error_count : 0, maximum);
    struct timeval timeout = {seconds, micros};
    int result = hw_select(n, maximum + 1, read ? &r : NULL, write ? &w : NULL, error ? &e : NULL,
                           infinite ? NULL : &timeout);
    if (result < 0)
        return fail();
    /* Apply the same pending-connect filter to real BSD sockets mixed with
       virtual descriptors. This preserves the guest's connect contract. */
    if (write)
        for (int i = 0; i < *write_count; i++) {
            int fd = write[i], pending = 0;
            socklen_t length = sizeof(pending);
            if (fd >= 0 && fd < FD_SETSIZE && FD_ISSET(fd, &w) && !web_owner(fd) &&
                !getsockopt(fd, SOL_SOCKET, SO_ERROR, &pending, &length) && pending) {
                FD_CLR(fd, &w);
                result--;
                errno = pending;
                fail();
            }
        }
    if (read)
        keep_ready(&r, read, read_count);
    if (write)
        keep_ready(&w, write, write_count);
    if (error)
        keep_ready(&e, error, error_count);
    if (result > 0)
        last_error = 0;
    return result;
}
