#ifndef PS3PSU_ICN_TEXTURE_H
#define PS3PSU_ICN_TEXTURE_H

#include <stddef.h>
#include <stdint.h>

/* Decode the size-prefixed RLE texture used by PS2 ICN files (128x128 BGR555). */
int ps2_icn_decode_rle(const uint8_t *src, size_t src_size, uint32_t *texture);

#endif
