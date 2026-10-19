#ifndef PS2_SAVE_PROBE_H
#define PS2_SAVE_PROBE_H
#include <stddef.h>
#include <stdint.h>
#include "ps2_save_format_detect.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Read-only file probe. Never modifies the input or trusts its extension. */
typedef struct {
    ps2_save_format format;
    uint64_t file_size;
    int signature_valid;
    int payload_present;
    int supported_decoder; /* remains zero until a decoder is integrated and tested */
} ps2_save_probe_result;
/* 0: inspected, -1: invalid arguments, -2: file I/O error */
int ps2_save_probe_file(const char *path, ps2_save_probe_result *result);
#ifdef __cplusplus
}
#endif
#endif
