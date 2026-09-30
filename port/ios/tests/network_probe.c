/* Darwin/Winsock boundary: connected UDP actions must reach the selected peer. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "posix.h"
struct guest_address { uint16_t family,port;uint32_t ip;unsigned char padding[8]; };
#define REQUIRE(test) do { if(!(test)){fprintf(stderr,"FAIL: %s, Winsock error %d (line %d)\n",#test,posix_socket_last_error(),__LINE__);return 1;} } while(0)
static int ready(int socket,int seconds) {
    int descriptors[]={socket},count=1,zero=0;
    return posix_socket_select(descriptors,&count,NULL,&zero,NULL,&zero,seconds,0,0)==1 && count==1;
}
int main(void) {
    int server=posix_socket(2,2,0),client=posix_socket(2,2,0),other=posix_socket(2,2,0),size;
    struct guest_address address={2,0,0x0100007f,{0}},peer,alternate=address;
    char buffer[32];
    REQUIRE(server>=0 && client>=0 && other>=0);
    REQUIRE(posix_socket_bind(server,&address,sizeof(address))==0);
    REQUIRE(posix_socket_bind(other,&alternate,sizeof(alternate))==0);
    size=sizeof(address);REQUIRE(posix_socket_getsockname(server,&address,&size)==0);
    REQUIRE(size==16 && address.family==2 && address.port!=0);
    size=sizeof(alternate);REQUIRE(posix_socket_getsockname(other,&alternate,&size)==0);
    REQUIRE(posix_socket_set_nonblocking(server,1)==0 && posix_socket_set_nonblocking(client,1)==0);
    REQUIRE(posix_socket_sendto(client,"first",5,0,&address,sizeof(address))==5);
    REQUIRE(ready(server,1));size=sizeof(peer);
    REQUIRE(posix_socket_recvfrom(server,buffer,sizeof(buffer),0,&peer,&size)==5 && !memcmp(buffer,"first",5));
    REQUIRE(peer.family==2 && peer.ip==address.ip && size==16);
    REQUIRE(posix_socket_connect(client,&address,sizeof(address))==0);
    REQUIRE(posix_socket_sendto(client,"action",6,0,&address,sizeof(address))==6);
    REQUIRE(ready(server,1));size=sizeof(peer);
    REQUIRE(posix_socket_recvfrom(server,buffer,sizeof(buffer),0,&peer,&size)==6 && !memcmp(buffer,"action",6));
    REQUIRE(posix_socket_sendto(server,"reply",5,0,&peer,sizeof(peer))==5);
    REQUIRE(ready(client,1));size=sizeof(peer);
    REQUIRE(posix_socket_recvfrom(client,buffer,sizeof(buffer),0,&peer,&size)==5 && !memcmp(buffer,"reply",5));
    REQUIRE(peer.family==2 && peer.port==address.port && peer.ip==address.ip);
    /* The same-peer fallback must not silently send a different destination. */
    REQUIRE(posix_socket_sendto(client,"wrong",5,0,&alternate,sizeof(alternate))==-1);
    REQUIRE(posix_socket_last_error()==10056 && !ready(other,0));
    posix_socket_close(server);posix_socket_close(client);posix_socket_close(other);
    puts("PASS: UDP actions/replies, connected destination handling, guest sockaddr conversion and select");
    return 0;
}
