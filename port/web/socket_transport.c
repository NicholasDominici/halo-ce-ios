#include "socket_transport.h"
#include "frames.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { SOCKETS = 128, PEERS = 128, QUEUE_BYTES = 65536, BACKLOG = 16 };
#define LOCAL_IP UINT32_C(0x7f000001)
struct endpoint {
    int used, fd, wake, type, nonblocking, listening, connected, eof, error, partner;
    uint32_t peer, stream;
    uint16_t local, remote;
    int pending[BACKLOG], pending_count, backlog;
    unsigned char *queue;
    size_t start, bytes;
};
struct peer {
    uint32_t ip;
    unsigned char identifier[6];
    int used, reliable, unreliable, writable;
};
struct hw_net {
    pthread_mutex_t lock;
    unsigned char identifier[6];
    struct endpoint sockets[SOCKETS];
    struct peer peers[PEERS];
    uint32_t next_stream;
    uint16_t next_port;
    int wake[2];
    hw_emit emit;
    void *context;
};
static int failed(int error) {
    errno = error;
    return -1;
}
static void notify(int fd) {
    char byte = 1;
    (void)send(fd, &byte, 1, 0);
}
static void unnotify(int fd) {
    char bytes[64];
    while (recv(fd, bytes, sizeof(bytes), 0) > 0) {
    }
}
static int handles(int pair[2]) {
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, pair))
        return -1;
    if (pair[0] >= FD_SETSIZE || pair[1] >= FD_SETSIZE) {
        close(pair[0]);
        close(pair[1]);
        return failed(EMFILE);
    }
    for (int i = 0; i < 2; i++) {
        fcntl(pair[i], F_SETFD, FD_CLOEXEC);
        fcntl(pair[i], F_SETFL, O_NONBLOCK);
    }
    return 0;
}
static struct endpoint *find(struct hw_net *n, int fd) {
    for (int i = 0; i < SOCKETS; i++)
        if (n->sockets[i].used && n->sockets[i].fd == fd)
            return &n->sockets[i];
    return NULL;
}
static struct peer *peer(struct hw_net *n, uint32_t ip) {
    for (int i = 0; i < PEERS; i++)
        if (n->peers[i].used && n->peers[i].ip == ip)
            return &n->peers[i];
    return NULL;
}
static int ready(struct hw_net *n, uint32_t ip, int reliable) {
    struct peer *p = peer(n, ip);
    return p && p->reliable && p->unreliable && (!reliable || p->writable);
}
static int port_used(struct hw_net *n, int type, uint16_t port) {
    for (int i = 0; i < SOCKETS; i++)
        if (n->sockets[i].used && n->sockets[i].type == type && n->sockets[i].local == port &&
            (type == SOCK_DGRAM || !n->sockets[i].connected))
            return 1;
    return 0;
}
static uint16_t ephemeral(struct hw_net *n, int type) {
    for (unsigned i = 0; i < 20000; i++) {
        if (++n->next_port < 40000)
            n->next_port = 40000;
        int used = 0;
        for (int j = 0; j < SOCKETS; j++)
            if (n->sockets[j].used && n->sockets[j].type == type &&
                n->sockets[j].local == n->next_port) {
                used = 1;
                break;
            }
        if (!used)
            return n->next_port;
    }
    return 0;
}
static struct endpoint *allocate(struct hw_net *n, int type) {
    for (int i = 0; i < SOCKETS; i++)
        if (!n->sockets[i].used) {
            int pair[2];
            unsigned char *queue = malloc(QUEUE_BYTES);
            if (!queue)
                return NULL;
            if (handles(pair)) {
                free(queue);
                return NULL;
            }
            struct endpoint *s = &n->sockets[i];
            memset(s, 0, sizeof(*s));
            s->used = 1;
            s->fd = pair[0];
            s->wake = pair[1];
            s->type = type;
            s->partner = -1;
            s->queue = queue;
            s->nonblocking = 1;
            return s;
        }
    errno = EMFILE;
    return NULL;
}
static void release(struct endpoint *s) {
    close(s->fd);
    close(s->wake);
    free(s->queue);
    memset(s, 0, sizeof(*s));
}
static void append(struct endpoint *s, const void *input, size_t bytes) {
    if (!bytes)
        return;
    const unsigned char *data = input;
    size_t end = (s->start + s->bytes) % QUEUE_BYTES, first = QUEUE_BYTES - end;
    if (first > bytes)
        first = bytes;
    memcpy(s->queue + end, data, first);
    memcpy(s->queue, data + first, bytes - first);
    if (!s->bytes && bytes)
        notify(s->wake);
    s->bytes += bytes;
}
static void copy(struct endpoint *s, void *output, size_t offset, size_t bytes) {
    if (!bytes)
        return;
    unsigned char *data = output;
    size_t start = (s->start + offset) % QUEUE_BYTES, first = QUEUE_BYTES - start;
    if (first > bytes)
        first = bytes;
    memcpy(data, s->queue + start, first);
    memcpy(data + first, s->queue, bytes - first);
}
static void consume(struct endpoint *s, size_t bytes) {
    s->start = (s->start + bytes) % QUEUE_BYTES;
    s->bytes -= bytes;
    if (!s->bytes && !s->eof)
        unnotify(s->fd);
}
static int datagram(struct hw_net *n, uint32_t ip, const struct hw_frame *f) {
    for (int i = 0; i < SOCKETS; i++) {
        struct endpoint *s = &n->sockets[i];
        if (!s->used || s->type != SOCK_DGRAM || s->local != f->destination ||
            (s->connected && (s->peer != ip || s->remote != f->source)))
            continue;
        if (QUEUE_BYTES - s->bytes >= f->size + 8) {
            unsigned char header[8] = {(unsigned char)(f->size >> 8),   (unsigned char)f->size,
                                       (unsigned char)(f->source >> 8), (unsigned char)f->source,
                                       (unsigned char)(ip >> 24),       (unsigned char)(ip >> 16),
                                       (unsigned char)(ip >> 8),        (unsigned char)ip};
            append(s, header, 8);
            append(s, f->payload, f->size);
        }
        break;
    }
    return 1;
}
static struct endpoint *listener(struct hw_net *n, uint16_t port) {
    for (int i = 0; i < SOCKETS; i++)
        if (n->sockets[i].used && n->sockets[i].listening && n->sockets[i].local == port)
            return &n->sockets[i];
    return NULL;
}
static struct endpoint *stream(struct hw_net *n, uint32_t ip, uint32_t id) {
    for (int i = 0; i < SOCKETS; i++)
        if (n->sockets[i].used && n->sockets[i].connected && n->sockets[i].type == SOCK_STREAM &&
            n->sockets[i].peer == ip && n->sockets[i].stream == id)
            return &n->sockets[i];
    return NULL;
}
static struct endpoint *incoming(struct hw_net *n, struct endpoint *listen, uint32_t ip,
                                 uint32_t id, uint16_t source) {
    if (listen->pending_count >= listen->backlog) {
        errno = EAGAIN;
        return NULL;
    }
    struct endpoint *s = allocate(n, SOCK_STREAM);
    if (!s)
        return NULL;
    s->local = listen->local;
    s->peer = ip;
    s->remote = source;
    s->stream = id;
    s->connected = 1;
    if (!listen->pending_count)
        notify(listen->wake);
    listen->pending[listen->pending_count++] = s->fd;
    return s;
}
struct hw_net *hw_net_create(const unsigned char identifier[6], hw_emit emit, void *context) {
    if (!identifier || !emit) {
        errno = EINVAL;
        return NULL;
    }
    struct hw_net *n = calloc(1, sizeof(*n));
    if (!n)
        return NULL;
    int error = pthread_mutex_init(&n->lock, NULL);
    if (error) {
        free(n);
        errno = error;
        return NULL;
    }
    if (handles(n->wake)) {
        pthread_mutex_destroy(&n->lock);
        free(n);
        return NULL;
    }
    memcpy(n->identifier, identifier, 6);
    n->next_stream = 0x40000000;
    n->next_port = 40000;
    n->emit = emit;
    n->context = context;
    return n;
}
void hw_net_destroy(struct hw_net *n) {
    if (!n)
        return;
    for (int i = 0; i < SOCKETS; i++)
        if (n->sockets[i].used)
            release(&n->sockets[i]);
    close(n->wake[0]);
    close(n->wake[1]);
    pthread_mutex_destroy(&n->lock);
    free(n);
}
uint32_t hw_net_add_peer(struct hw_net *n, const unsigned char identifier[6]) {
    if (!identifier || !memcmp(n->identifier, identifier, 6))
        return 0;
    pthread_mutex_lock(&n->lock);
    uint32_t result = 0;
    for (int i = 0; i < PEERS; i++)
        if (n->peers[i].used && !memcmp(n->peers[i].identifier, identifier, 6)) {
            result = n->peers[i].ip;
            break;
        }
    if (!result)
        for (int i = 0; i < PEERS; i++)
            if (!n->peers[i].used) {
                struct peer *p = &n->peers[i];
                p->used = 1;
                p->ip = 0x7f001001 + (uint32_t)i;
                memcpy(p->identifier, identifier, 6);
                result = p->ip;
                break;
            }
    pthread_mutex_unlock(&n->lock);
    return result;
}
void hw_net_peer_state(struct hw_net *n, uint32_t ip, int reliable, int unreliable, int writable) {
    pthread_mutex_lock(&n->lock);
    struct peer *p = peer(n, ip);
    if (p &&
        (p->reliable != !!reliable || p->unreliable != !!unreliable || p->writable != !!writable)) {
        p->reliable = !!reliable;
        p->unreliable = !!unreliable;
        p->writable = !!writable;
        notify(n->wake[1]);
    }
    pthread_mutex_unlock(&n->lock);
}
void hw_net_remove_peer(struct hw_net *n, uint32_t ip) {
    pthread_mutex_lock(&n->lock);
    struct peer *p = peer(n, ip);
    if (p)
        memset(p, 0, sizeof(*p));
    for (int i = 0; i < SOCKETS; i++) {
        struct endpoint *s = &n->sockets[i];
        if (s->used && s->connected && s->peer == ip) {
            s->eof = 1;
            s->error = ECONNRESET;
            notify(s->wake);
        }
    }
    notify(n->wake[1]);
    pthread_mutex_unlock(&n->lock);
}
int hw_net_peer_ready(struct hw_net *n, uint32_t ip) {
    pthread_mutex_lock(&n->lock);
    int result = ready(n, ip, 0);
    pthread_mutex_unlock(&n->lock);
    return result;
}
int hw_net_receive(struct hw_net *n, uint32_t ip, const void *bytes, size_t size) {
    struct hw_frame f;
    if (hw_frame_decode(bytes, size, &f))
        return -1;
    pthread_mutex_lock(&n->lock);
    if (!peer(n, ip)) {
        pthread_mutex_unlock(&n->lock);
        return -1;
    }
    int result = 1;
    if (f.type == HWF_DATAGRAM)
        result = datagram(n, ip, &f);
    else {
        struct endpoint *s = stream(n, ip, f.stream);
        if (f.type == HWF_OPEN) {
            if (!s) {
                struct endpoint *l = listener(n, f.destination);
                if (l && !incoming(n, l, ip, f.stream, f.source))
                    result = 0;
            }
        } else if (s && f.type == HWF_CLOSE) {
            s->eof = 1;
            notify(s->wake);
        } else if (s && f.type == HWF_DATA && !s->eof) {
            if (f.size > QUEUE_BYTES - s->bytes)
                result = 0;
            else
                append(s, f.payload, f.size);
        }
    }
    pthread_mutex_unlock(&n->lock);
    return result;
}
int hw_socket(struct hw_net *n, int type) {
    if (type != SOCK_STREAM && type != SOCK_DGRAM)
        return failed(EPROTOTYPE);
    pthread_mutex_lock(&n->lock);
    struct endpoint *s = allocate(n, type);
    int fd = s ? s->fd : -1;
    pthread_mutex_unlock(&n->lock);
    return fd;
}
int hw_owns_socket(struct hw_net *n, int fd) {
    pthread_mutex_lock(&n->lock);
    int yes = find(n, fd) != NULL;
    pthread_mutex_unlock(&n->lock);
    return yes;
}
int hw_socket_count(struct hw_net *n) {
    pthread_mutex_lock(&n->lock);
    int count = 0;
    for (int i = 0; i < SOCKETS; i++)
        count += !!n->sockets[i].used;
    pthread_mutex_unlock(&n->lock);
    return count;
}
int hw_close(struct hw_net *n, int fd) {
    pthread_mutex_lock(&n->lock);
    struct endpoint *s = find(n, fd);
    if (!s) {
        pthread_mutex_unlock(&n->lock);
        return failed(EBADF);
    }
    uint32_t ip = s->peer, id = s->stream;
    int send_close = s->connected && s->type == SOCK_STREAM && s->partner < 0 && !s->eof;
    struct endpoint *partner = find(n, s->partner);
    if (partner) {
        partner->eof = 1;
        partner->partner = -1;
        notify(partner->wake);
    }
    int pending[BACKLOG], pending_count = s->pending_count;
    memcpy(pending, s->pending, (size_t)pending_count * sizeof(int));
    release(s);
    notify(n->wake[1]);
    pthread_mutex_unlock(&n->lock);
    if (send_close) {
        unsigned char bytes[HWF_HEADER];
        struct hw_frame f = {HWF_CLOSE, id, 0, 0, NULL, 0};
        hw_frame_encode(bytes, sizeof(bytes), &f);
        n->emit(n->context, ip, 1, bytes, sizeof(bytes));
    }
    for (int i = 0; i < pending_count; i++)
        hw_close(n, pending[i]);
    return 0;
}
int hw_bind(struct hw_net *n, int fd, uint32_t ip, uint16_t port) {
    pthread_mutex_lock(&n->lock);
    struct endpoint *s = find(n, fd);
    int error = 0;
    if (!s)
        error = EBADF;
    else if (ip && ip != LOCAL_IP)
        error = EADDRNOTAVAIL;
    else if (s->local || s->connected)
        error = EINVAL;
    else if (port && port_used(n, s->type, port))
        error = EADDRINUSE;
    else if (!(s->local = port ? port : ephemeral(n, s->type)))
        error = EADDRINUSE;
    pthread_mutex_unlock(&n->lock);
    return error ? failed(error) : 0;
}
int hw_listen(struct hw_net *n, int fd, int backlog) {
    pthread_mutex_lock(&n->lock);
    struct endpoint *s = find(n, fd);
    int error = 0;
    if (!s)
        error = EBADF;
    else if (s->type != SOCK_STREAM || s->connected)
        error = EINVAL;
    else {
        if (!s->local)
            s->local = ephemeral(n, s->type);
        s->listening = 1;
        s->backlog = backlog < 1 ? 1 : backlog > BACKLOG ? BACKLOG : backlog;
    }
    pthread_mutex_unlock(&n->lock);
    return error ? failed(error) : 0;
}
int hw_connect(struct hw_net *n, int fd, uint32_t ip, uint16_t port) {
    pthread_mutex_lock(&n->lock);
    struct endpoint *s = find(n, fd);
    int error = 0;
    if (!s)
        error = EBADF;
    else if (!port)
        error = EINVAL;
    else if (s->connected || s->listening)
        error = EISCONN;
    else if (ip != LOCAL_IP && !peer(n, ip))
        error = ENETUNREACH;
    if (error) {
        pthread_mutex_unlock(&n->lock);
        return failed(error);
    }
    if (!s->local)
        s->local = ephemeral(n, s->type);
    s->peer = ip;
    s->remote = port;
    if (s->type == SOCK_DGRAM) {
        s->connected = 1;
        pthread_mutex_unlock(&n->lock);
        return 0;
    }
    if (ip == LOCAL_IP) {
        struct endpoint *l = listener(n, port),
                        *other = l ? incoming(n, l, LOCAL_IP, 0, s->local) : NULL;
        if (other) {
            s->partner = other->fd;
            other->partner = s->fd;
            s->connected = 1;
        }
        pthread_mutex_unlock(&n->lock);
        return other ? 0 : failed(ECONNREFUSED);
    }
    if (!ready(n, ip, 1)) {
        pthread_mutex_unlock(&n->lock);
        return failed(EAGAIN);
    }
    /* Native peers use opposite parity. The high range also avoids the small
       IDs observed in the web deployment's initial stream allocations. */
    n->next_stream += 2;
    uint32_t id =
        n->next_stream | (memcmp(n->identifier, peer(n, ip)->identifier, 6) > 0 ? 1u : 0u);
    uint16_t source = s->local;
    s->stream = id;
    s->connected = 1;
    pthread_mutex_unlock(&n->lock);
    unsigned char bytes[HWF_HEADER];
    struct hw_frame f = {HWF_OPEN, id, source, port, NULL, 0};
    hw_frame_encode(bytes, sizeof(bytes), &f);
    int sent = n->emit(n->context, ip, 1, bytes, sizeof(bytes));
    pthread_mutex_lock(&n->lock);
    s = find(n, fd);
    if (s) {
        if (sent != 1) {
            s->stream = 0;
            s->connected = 0;
        }
        notify(n->wake[1]);
    }
    pthread_mutex_unlock(&n->lock);
    if (sent != 1)
        return failed(sent < 0 ? ENETUNREACH : EAGAIN);
    return s ? 0 : failed(EBADF);
}
int hw_accept(struct hw_net *n, int fd, uint32_t *ip, uint16_t *port) {
    pthread_mutex_lock(&n->lock);
    struct endpoint *s = find(n, fd);
    int result = -1, error = 0;
    if (!s)
        error = EBADF;
    else if (!s->listening)
        error = EINVAL;
    else if (!s->pending_count)
        error = EAGAIN;
    else {
        result = s->pending[0];
        memmove(s->pending, s->pending + 1, (size_t)(--s->pending_count) * sizeof(int));
        if (!s->pending_count)
            unnotify(s->fd);
        struct endpoint *other = find(n, result);
        if (ip)
            *ip = other->peer;
        if (port)
            *port = other->remote;
    }
    pthread_mutex_unlock(&n->lock);
    return error ? failed(error) : result;
}
int hw_sendto(struct hw_net *n, int fd, const void *bytes, size_t size, int flags, uint32_t ip,
              uint16_t port) {
    if ((size && !bytes) || flags || size > HWF_MAX_DATAGRAM || !port)
        return failed(size > HWF_MAX_DATAGRAM ? EMSGSIZE : EINVAL);
    pthread_mutex_lock(&n->lock);
    struct endpoint *s = find(n, fd);
    int error = 0;
    if (!s)
        error = EBADF;
    else if (s->type != SOCK_DGRAM)
        error = EPROTOTYPE;
    else if (ip != LOCAL_IP && ip != UINT32_MAX && !ready(n, ip, 0))
        error = ENETUNREACH;
    if (error) {
        pthread_mutex_unlock(&n->lock);
        return failed(error);
    }
    if (!s->local)
        s->local = ephemeral(n, s->type);
    struct hw_frame f = {HWF_DATAGRAM, 0, s->local, port, bytes, size};
    if (ip == LOCAL_IP) {
        datagram(n, LOCAL_IP, &f);
        pthread_mutex_unlock(&n->lock);
        return (int)size;
    }
    /* System-link discovery broadcasts are not room admission. */
    if (ip == UINT32_MAX) {
        pthread_mutex_unlock(&n->lock);
        return (int)size;
    }
    pthread_mutex_unlock(&n->lock);
    unsigned char frame[HWF_HEADER + HWF_MAX_DATAGRAM];
    int count = hw_frame_encode(frame, sizeof(frame), &f);
    int sent = n->emit(n->context, ip, 0, frame, (size_t)count);
    return sent == 1 ? (int)size : failed(sent < 0 ? ENETUNREACH : EAGAIN);
}
int hw_send(struct hw_net *n, int fd, const void *bytes, size_t size, int flags) {
    if ((size && !bytes) || flags)
        return failed(EINVAL);
    pthread_mutex_lock(&n->lock);
    struct endpoint *s = find(n, fd);
    if (!s || !s->connected || s->eof) {
        int error = !s ? EBADF : s->eof ? EPIPE : ENOTCONN;
        pthread_mutex_unlock(&n->lock);
        return failed(error);
    }
    uint32_t ip = s->peer, id = s->stream;
    uint16_t port = s->remote;
    if (s->type == SOCK_DGRAM) {
        pthread_mutex_unlock(&n->lock);
        return hw_sendto(n, fd, bytes, size, flags, ip, port);
    }
    if (!size) {
        pthread_mutex_unlock(&n->lock);
        return 0;
    }
    if (s->partner >= 0) {
        struct endpoint *other = find(n, s->partner);
        if (!other) {
            pthread_mutex_unlock(&n->lock);
            return failed(EPIPE);
        }
        size_t count = QUEUE_BYTES - other->bytes;
        if (count > size)
            count = size;
        append(other, bytes, count);
        pthread_mutex_unlock(&n->lock);
        return count ? (int)count : failed(EAGAIN);
    }
    if (!ready(n, ip, 1)) {
        pthread_mutex_unlock(&n->lock);
        return failed(EAGAIN);
    }
    pthread_mutex_unlock(&n->lock);
    if (size > HWF_MAX_PAYLOAD)
        size = HWF_MAX_PAYLOAD;
    unsigned char frame[HWF_HEADER + HWF_MAX_PAYLOAD];
    struct hw_frame f = {HWF_DATA, id, 0, 0, bytes, size};
    int count = hw_frame_encode(frame, sizeof(frame), &f);
    int sent = n->emit(n->context, ip, 1, frame, (size_t)count);
    return sent == 1 ? (int)size : failed(sent < 0 ? ENETUNREACH : EAGAIN);
}
int hw_recvfrom(struct hw_net *n, int fd, void *bytes, size_t size, int flags, uint32_t *ip,
                uint16_t *port) {
    if ((size && !bytes) || (flags & ~MSG_PEEK))
        return failed(EINVAL);
    pthread_mutex_lock(&n->lock);
    struct endpoint *s = find(n, fd);
    if (!s) {
        pthread_mutex_unlock(&n->lock);
        return failed(EBADF);
    }
    if (!size && s->type == SOCK_STREAM) {
        pthread_mutex_unlock(&n->lock);
        return 0;
    }
    if (!s->bytes) {
        int result = s->eof && !s->error ? 0 : -1;
        int error = s->error ? s->error : EAGAIN;
        pthread_mutex_unlock(&n->lock);
        return result < 0 ? failed(error) : 0;
    }
    size_t count = s->bytes, consumed = count, offset = 0;
    if (s->type == SOCK_DGRAM) {
        unsigned char header[8];
        copy(s, header, 0, 8);
        count = (size_t)((header[0] << 8) | header[1]);
        consumed = count + 8;
        offset = 8;
        if (ip)
            *ip = ((uint32_t)header[4] << 24) | ((uint32_t)header[5] << 16) |
                  ((uint32_t)header[6] << 8) | header[7];
        if (port)
            *port = (uint16_t)((header[2] << 8) | header[3]);
    } else {
        if (ip)
            *ip = s->peer;
        if (port)
            *port = s->remote;
    }
    if (count > size)
        count = size;
    copy(s, bytes, offset, count);
    if (!(flags & MSG_PEEK))
        consume(s, s->type == SOCK_DGRAM ? consumed : count);
    notify(n->wake[1]);
    pthread_mutex_unlock(&n->lock);
    return (int)count;
}
int hw_recv(struct hw_net *n, int fd, void *bytes, size_t size, int flags) {
    return hw_recvfrom(n, fd, bytes, size, flags, NULL, NULL);
}
int hw_shutdown(struct hw_net *n, int fd, int how) {
    if (how < 0 || how > 2)
        return failed(EINVAL);
    /* The observed protocol has full stream close, not a TCP half-close. */
    pthread_mutex_lock(&n->lock);
    struct endpoint *s = find(n, fd);
    if (!s) {
        pthread_mutex_unlock(&n->lock);
        return failed(EBADF);
    }
    s->eof = 1;
    notify(s->wake);
    struct endpoint *other = find(n, s->partner);
    if (other) {
        other->eof = 1;
        notify(other->wake);
    }
    uint32_t ip = s->peer, id = s->stream;
    int emit = s->connected && s->type == SOCK_STREAM && s->partner < 0;
    pthread_mutex_unlock(&n->lock);
    if (emit) {
        unsigned char bytes[HWF_HEADER];
        struct hw_frame f = {HWF_CLOSE, id, 0, 0, NULL, 0};
        hw_frame_encode(bytes, sizeof(bytes), &f);
        n->emit(n->context, ip, 1, bytes, sizeof(bytes));
    }
    return 0;
}
int hw_nonblocking(struct hw_net *n, int fd, int enabled) {
    /* Halo uses nonblocking sockets. Blocking waits belong in hw_select. */
    if (!enabled)
        return failed(EOPNOTSUPP);
    return hw_owns_socket(n, fd) ? 0 : failed(EBADF);
}
int hw_available(struct hw_net *n, int fd, int *bytes) {
    pthread_mutex_lock(&n->lock);
    struct endpoint *s = find(n, fd);
    if (s && bytes) {
        *bytes = (int)s->bytes;
        if (s->type == SOCK_DGRAM && s->bytes) {
            unsigned char header[2];
            copy(s, header, 0, 2);
            *bytes = (header[0] << 8) | header[1];
        }
    }
    pthread_mutex_unlock(&n->lock);
    return s && bytes ? 0 : failed(s ? EINVAL : EBADF);
}
int hw_name(struct hw_net *n, int fd, int remote, uint32_t *ip, uint16_t *port) {
    pthread_mutex_lock(&n->lock);
    struct endpoint *s = find(n, fd);
    int error = !s ? EBADF : remote && !s->connected ? ENOTCONN : 0;
    if (!error) {
        if (ip)
            *ip = remote ? s->peer : LOCAL_IP;
        if (port)
            *port = remote ? s->remote : s->local;
    }
    pthread_mutex_unlock(&n->lock);
    return error ? failed(error) : 0;
}
int hw_option(struct hw_net *n, int fd, int option, int *value) {
    pthread_mutex_lock(&n->lock);
    struct endpoint *s = find(n, fd);
    int error = 0;
    if (!s || !value)
        error = s ? EINVAL : EBADF;
    else if (option == SO_TYPE)
        *value = s->type;
    else if (option == SO_ERROR)
        *value = s->error;
    else if (option == SO_SNDBUF || option == SO_RCVBUF)
        *value = QUEUE_BYTES;
    else
        error = ENOPROTOOPT;
    pthread_mutex_unlock(&n->lock);
    return error ? failed(error) : 0;
}
static int writable(struct hw_net *n, struct endpoint *s) {
    if (s->error || s->eof || s->listening)
        return 0;
    if (s->type == SOCK_DGRAM)
        return 1;
    if (!s->connected)
        return 0;
    if (s->partner >= 0) {
        struct endpoint *other = find(n, s->partner);
        return other && other->bytes < QUEUE_BYTES;
    }
    return ready(n, s->peer, 1);
}
static double monotonic(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
int hw_select(struct hw_net *n, int limit, fd_set *read, fd_set *write, fd_set *error,
              struct timeval *timeout) {
    if (limit < 0 || limit > FD_SETSIZE)
        return failed(EINVAL);
    if (timeout && (timeout->tv_sec < 0 || timeout->tv_usec < 0 || timeout->tv_usec >= 1000000))
        return failed(EINVAL);
    fd_set wanted_read, wanted_write, wanted_error;
    FD_ZERO(&wanted_read);
    FD_ZERO(&wanted_write);
    FD_ZERO(&wanted_error);
    if (read)
        wanted_read = *read;
    if (write)
        wanted_write = *write;
    if (error)
        wanted_error = *error;
    double deadline = timeout ? monotonic() + timeout->tv_sec + timeout->tv_usec / 1e6 : 0;
    while (1) {
        fd_set r = wanted_read, w = wanted_write, e = wanted_error;
        FD_SET(n->wake[0], &r);
        pthread_mutex_lock(&n->lock);
        for (int fd = 0; fd < limit; fd++) {
            struct endpoint *s = find(n, fd);
            if (s) {
                if (FD_ISSET(fd, &w) && !writable(n, s))
                    FD_CLR(fd, &w);
                FD_CLR(fd, &e);
            }
        }
        pthread_mutex_unlock(&n->lock);
        struct timeval remaining, *wait = NULL;
        if (timeout) {
            double left = deadline - monotonic();
            if (left < 0)
                left = 0;
            remaining.tv_sec = (time_t)left;
            remaining.tv_usec = (suseconds_t)((left - remaining.tv_sec) * 1e6);
            wait = &remaining;
        }
        int max = limit;
        if (n->wake[0] >= max)
            max = n->wake[0] + 1;
        int result = select(max, &r, &w, &e, wait);
        if (result < 0)
            return -1;
        if (FD_ISSET(n->wake[0], &r)) {
            unnotify(n->wake[0]);
            FD_CLR(n->wake[0], &r);
        }
        pthread_mutex_lock(&n->lock);
        int count = 0;
        for (int fd = 0; fd < limit; fd++) {
            struct endpoint *s = find(n, fd);
            if (s) {
                if (FD_ISSET(fd, &wanted_write) && writable(n, s))
                    FD_SET(fd, &w);
                else
                    FD_CLR(fd, &w);
                if (FD_ISSET(fd, &wanted_error) && s->error)
                    FD_SET(fd, &e);
            }
            if (FD_ISSET(fd, &r))
                count++;
            if (FD_ISSET(fd, &w))
                count++;
            if (FD_ISSET(fd, &e))
                count++;
        }
        pthread_mutex_unlock(&n->lock);
        if (count || (timeout && monotonic() >= deadline)) {
            if (read)
                *read = r;
            if (write)
                *write = w;
            if (error)
                *error = e;
            return count;
        }
    }
}
