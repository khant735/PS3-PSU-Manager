#ifndef PS2_SAVE_FORMAT_DETECT_H
#define PS2_SAVE_FORMAT_DETECT_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { PS2_SAVE_UNKNOWN=0, PS2_SAVE_MAX, PS2_SAVE_CBS, PS2_SAVE_XPS_SPS } ps2_save_format;
/* Read-only signature identification. Does NOT claim decoding or compatibility. */
ps2_save_format ps2_save_detect_header(const unsigned char *data, size_t size);
const char *ps2_save_format_label(ps2_save_format fmt);
#ifdef __cplusplus
}
#endif
#endif
