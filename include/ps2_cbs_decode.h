#ifndef PS2_CBS_DECODE_H
#define PS2_CBS_DECODE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Decodes CBS payload to caller-owned buffer; no filesystem writes. 0 success. */
int ps2_cbs_decode(const unsigned char *archive, size_t length,
                   unsigned char *output, size_t capacity, size_t *written);
#ifdef __cplusplus
}
#endif
#endif
