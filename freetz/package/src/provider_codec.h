// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef TVB_PROVIDER_CODEC_H
#define TVB_PROVIDER_CODEC_H
#include <stddef.h>
#include <stdint.h>

/* AHA's outer protocol uses network byte order. HAN-FUN interface words have
 * a receiver create/update asymmetry, handled at identity substitution. These
 * helpers permit unaligned buffers; callers validate the available length. */
static inline unsigned read_be16(const unsigned char *p) {
    return ((unsigned)p[0] << 8) | p[1];
}
static inline uint32_t read_be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static inline void write_be16(unsigned char *p, unsigned value) {
    p[0] = value >> 8;
    p[1] = value;
}
static inline void write_be32(unsigned char *p, uint32_t value) {
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

static inline void write_be64(unsigned char *p, uint64_t value) {
    write_be32(p, (uint32_t)(value >> 32));
    write_be32(p + 4, (uint32_t)value);
}

/* Validate the WHOLE batch before invoking any handler with side effects.
 * Incomplete or unfamiliar buffers belong to the original native sender. */
static inline int provider_complete_batch(const unsigned char *p, size_t size) {
    size_t offset = 0;
    if (size < 16) return 0;
    while (offset < size) {
        if (size - offset < 16 || p[offset + 1] != 3) return 0;
        unsigned length = read_be16(p + offset + 2);
        if (length < 16 || length > size - offset) return 0;
        offset += length;
    }
    return 1;
}
#endif
