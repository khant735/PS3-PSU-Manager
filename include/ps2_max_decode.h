#ifndef PS2_MAX_DECODE_H
#define PS2_MAX_DECODE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Decode an Action Replay MAX LZARI stream. The first four stream bytes
   contain the little-endian uncompressed size. No filesystem operations.
   Returns 0 on success, negative on malformed input or insufficient space. */
int ps2_max_lzari_decode(const uint8_t *src, size_t src_len,
                         uint8_t *dst, size_t dst_capacity, size_t *written);
/* Parse the MAX archive header and decode its payload. */
int ps2_max_archive_decode(const uint8_t *archive, size_t archive_len,
                           uint8_t *dst, size_t dst_capacity, size_t *written);
#ifdef __cplusplus
}
#endif
#endif
