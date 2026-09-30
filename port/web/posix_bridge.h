#ifndef HALO_WEB_POSIX_BRIDGE_H
#define HALO_WEB_POSIX_BRIDGE_H
#include "socket_transport.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Opt-in integration point for the iOS Winsock boundary. Install before game
   sockets are created, on the game/control thread. Close every virtual socket
   before replacing/detaching, and detach before destroying net. Admission and
   room/engine lifecycle remain the caller's responsibility. NULL restores BSD.
   A transport change with live virtual sockets fails with EBUSY. */
int halo_web_install_transport(struct hw_net *net);
#ifdef __cplusplus
}
#endif
#endif
