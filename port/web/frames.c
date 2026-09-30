/* Layout inferred from the fingerprinted public web deployment, not a stable ABI. */
#include "frames.h"
#include <string.h>
static int valid(const struct hw_frame *f) {
    if (f->size > HWF_MAX_PAYLOAD || (f->size && !f->payload))
        return 0;
    switch (f->type) {
    case HWF_DATAGRAM:
        return !f->stream && f->source && f->destination && f->size <= HWF_MAX_DATAGRAM;
    case HWF_OPEN:
        return f->stream && f->source && f->destination && !f->size;
    case HWF_DATA:
        return f->stream && !f->source && !f->destination;
    case HWF_CLOSE:
        return f->stream && !f->source && !f->destination && !f->size;
    default:
        return 0;
    }
}
int hw_frame_decode(const void *input, size_t size, struct hw_frame *f) {
    const unsigned char *b = input;
    if (!b || !f || size < HWF_HEADER || size > HWF_HEADER + HWF_MAX_PAYLOAD || b[0] != 0x48 ||
        b[1] != 1 || b[3])
        return -1;
    f->type = b[2];
    f->stream = ((uint32_t)b[4] << 24) | ((uint32_t)b[5] << 16) | ((uint32_t)b[6] << 8) | b[7];
    f->source = (uint16_t)((b[8] << 8) | b[9]);
    f->destination = (uint16_t)((b[10] << 8) | b[11]);
    f->payload = b + HWF_HEADER;
    f->size = size - HWF_HEADER;
    return valid(f) ? 0 : -1;
}
int hw_frame_encode(void *output, size_t capacity, const struct hw_frame *f) {
    unsigned char *b = output;
    if (!b || !f || !valid(f) || capacity < HWF_HEADER + f->size)
        return -1;
    b[0] = 0x48;
    b[1] = 1;
    b[2] = (unsigned char)f->type;
    b[3] = 0;
    b[4] = (unsigned char)(f->stream >> 24);
    b[5] = (unsigned char)(f->stream >> 16);
    b[6] = (unsigned char)(f->stream >> 8);
    b[7] = (unsigned char)f->stream;
    b[8] = (unsigned char)(f->source >> 8);
    b[9] = (unsigned char)f->source;
    b[10] = (unsigned char)(f->destination >> 8);
    b[11] = (unsigned char)f->destination;
    if (f->size)
        memcpy(b + HWF_HEADER, f->payload, f->size);
    return (int)(HWF_HEADER + f->size);
}
