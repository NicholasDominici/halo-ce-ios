/* Adapt Linux socket helpers to Darwin's sockaddr length byte and socket flags. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0x80000
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
static struct sockaddr_storage sockaddr_inward(const void *input,socklen_t size) {
    struct sockaddr_storage address={0};
    if(size>sizeof(address))size=sizeof(address);
    if(input && size>=2){memcpy(&address,input,size);uint16_t family;memcpy(&family,input,2);address.ss_len=size;address.ss_family=family;}
    return address;
}
static void sockaddr_outward(void *address,socklen_t size) {
    if(address && size>=2){uint16_t family=((struct sockaddr *)address)->sa_family;memcpy(address,&family,2);}
}
static int ios_socket(int family,int type,int protocol) {
    int fd=socket(family,type&~SOCK_CLOEXEC,protocol);
    if(fd>=0){int one=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));fcntl(fd,F_SETFD,FD_CLOEXEC);}return fd;
}
static int ios_bind(int fd,const void *p,socklen_t n) {struct sockaddr_storage a=sockaddr_inward(p,n);return bind(fd,(struct sockaddr *)&a,n);}
static int ios_connect(int fd,const void *p,socklen_t n) {struct sockaddr_storage a=sockaddr_inward(p,n);return connect(fd,(struct sockaddr *)&a,n);}
static int ios_accept4(int fd,void *p,socklen_t *n,int flags) {
    (void)flags;int out=accept(fd,p,n);if(out>=0){fcntl(out,F_SETFD,FD_CLOEXEC);if(n)sockaddr_outward(p,*n);}return out;
}
static ssize_t ios_sendto(int fd,const void *p,size_t n,int flags,const void *address,socklen_t size) {
    struct sockaddr_storage a=sockaddr_inward(address,size);
    ssize_t out=sendto(fd,p,n,flags,(struct sockaddr *)&a,size);
    /* Darwin rejects a destination on a connected datagram socket. Winsock
       accepts it. Use send only when the requested destination is the peer;
       never silently redirect a datagram meant for a different endpoint. */
    if(out<0 && errno==EISCONN && a.ss_family==AF_INET && size>=sizeof(struct sockaddr_in)) {
        struct sockaddr_in peer; socklen_t peer_size=sizeof(peer);
        int type=0; socklen_t type_size=sizeof(type);
        const struct sockaddr_in *target=(const struct sockaddr_in *)&a;
        if(!getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&type_size) && type==SOCK_DGRAM &&
           !getpeername(fd,(struct sockaddr *)&peer,&peer_size) && peer.sin_family==AF_INET &&
           peer.sin_addr.s_addr==target->sin_addr.s_addr && peer.sin_port==target->sin_port)
            out=send(fd,p,n,flags);
        else errno=EISCONN;
    }
    return out;
}
static ssize_t ios_recvfrom(int fd,void *p,size_t n,int flags,void *address,socklen_t *size) {
    ssize_t out=recvfrom(fd,p,n,flags,address,size);if(out>=0 && size)sockaddr_outward(address,*size);return out;
}
static int ios_getsockname(int fd,void *p,socklen_t *n) {int r=getsockname(fd,p,n);if(!r)sockaddr_outward(p,*n);return r;}
static int ios_getpeername(int fd,void *p,socklen_t *n) {int r=getpeername(fd,p,n);if(!r)sockaddr_outward(p,*n);return r;}
static ssize_t ios_getrandom(void *p,size_t size,unsigned flags) {(void)flags;arc4random_buf(p,size);return size;}
#define socket ios_socket
#define bind ios_bind
#define connect ios_connect
#define accept4 ios_accept4
#define sendto ios_sendto
#define recvfrom ios_recvfrom
#define getsockname ios_getsockname
#define getpeername ios_getpeername
#define getrandom ios_getrandom
#ifdef HALO_ENABLE_WEBRTC
#include "../../web/posix_bridge.h"
#define posix_socket bsd_posix_socket
#define posix_socket_close bsd_posix_socket_close
#define posix_socket_bind bsd_posix_socket_bind
#define posix_socket_connect bsd_posix_socket_connect
#define posix_socket_listen bsd_posix_socket_listen
#define posix_socket_accept bsd_posix_socket_accept
#define posix_socket_send bsd_posix_socket_send
#define posix_socket_sendto bsd_posix_socket_sendto
#define posix_socket_recv bsd_posix_socket_recv
#define posix_socket_recvfrom bsd_posix_socket_recvfrom
#define posix_socket_shutdown bsd_posix_socket_shutdown
#define posix_socket_set_nonblocking bsd_posix_socket_set_nonblocking
#define posix_socket_bytes_available bsd_posix_socket_bytes_available
#define posix_socket_setsockopt bsd_posix_socket_setsockopt
#define posix_socket_getsockopt bsd_posix_socket_getsockopt
#define posix_socket_getsockname bsd_posix_socket_getsockname
#define posix_socket_getpeername bsd_posix_socket_getpeername
#define posix_socket_select bsd_posix_socket_select
#define posix_local_ipv4_address bsd_posix_local_ipv4_address
#endif
#include "../../linux/src/posix_net.c"
#ifdef HALO_ENABLE_WEBRTC
#undef posix_socket
#undef posix_socket_close
#undef posix_socket_bind
#undef posix_socket_connect
#undef posix_socket_listen
#undef posix_socket_accept
#undef posix_socket_send
#undef posix_socket_sendto
#undef posix_socket_recv
#undef posix_socket_recvfrom
#undef posix_socket_shutdown
#undef posix_socket_set_nonblocking
#undef posix_socket_bytes_available
#undef posix_socket_setsockopt
#undef posix_socket_getsockopt
#undef posix_socket_getsockname
#undef posix_socket_getpeername
#undef posix_socket_select
#undef posix_local_ipv4_address
#undef socket
#undef bind
#undef connect
#undef accept4
#undef sendto
#undef recvfrom
#undef getsockname
#undef getpeername
#undef getrandom
#include "../../web/posix_bridge.inc"
#endif
