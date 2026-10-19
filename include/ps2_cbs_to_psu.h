#ifndef PS2_CBS_TO_PSU_H
#define PS2_CBS_TO_PSU_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Opt-in conversion; never overwrites output. Uses existing psu_create_from_dir.
   0 success, negative on invalid input, collision or I/O failure.
   Caller supplies decoded CBS archive bytes (not a path). */
int ps2_cbs_convert_to_psu(const unsigned char *archive, size_t archive_size,
                           const char *output_psu);
#ifdef __cplusplus
}
#endif
#endif
