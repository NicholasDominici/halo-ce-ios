#ifndef HALO_WEB_FRAMES_H
#define HALO_WEB_FRAMES_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { HWF_HEADER = 12, HWF_MAX_PAYLOAD = 16384, HWF_MAX_DATAGRAM = 1500 };
enum { HWF_DATAGRAM = 1, HWF_OPEN = 2, HWF_DATA = 3, HWF_CLOSE = 4 };
struct hw_frame {
    unsigned type;
    uint32_t stream;
    uint16_t source, destination;
    const unsigned char *payload;
    size_t size;
};
int hw_frame_decode(const void *bytes, size_t size, struct hw_frame *frame);
int hw_frame_encode(void *bytes, size_t capacity, const struct hw_frame *frame);
#ifdef __cplusplus
}
#endif
#endif
