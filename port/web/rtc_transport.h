#ifndef HALO_WEB_RTC_TRANSPORT_H
#define HALO_WEB_RTC_TRANSPORT_H
#include "socket_transport.h"
#ifdef __cplusplus
extern "C" {
#endif
struct hw_rtc;
/* description(value SDP, detail offer/answer), candidate(value ICE, detail MID),
   ready, disconnected, or error. The caller owns supported room admission and
   must copy callback strings before returning. Do not destroy rtc from inside
   a callback; stop game users and destroy on the owning control thread.
   No service URL is hardcoded. */
typedef void (*hw_signal)(void *context, const char *event, const char *value, const char *detail);
struct hw_rtc *hw_rtc_create(const unsigned char local[6], const unsigned char remote[6],
                             int initiator, hw_signal signal, void *context);
struct hw_net *hw_rtc_net(struct hw_rtc *rtc);
uint32_t hw_rtc_peer(struct hw_rtc *rtc);
int hw_rtc_description(struct hw_rtc *rtc, const char *sdp, const char *type);
int hw_rtc_candidate(struct hw_rtc *rtc, const char *candidate, const char *mid);
void hw_rtc_destroy(struct hw_rtc *rtc);
#ifdef __cplusplus
}
#endif
#endif
