#ifndef PS2_MAX_TO_PSU_H
#define PS2_MAX_TO_PSU_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Explicit MAX -> PSU conversion only. Never overwrites destination.
   Uses existing PSU writer unchanged. Returns zero on success. */
int ps2_max_convert_to_psu(const unsigned char *archive, size_t length,
                           const char *output_psu);
#ifdef __cplusplus
}
#endif
#endif
