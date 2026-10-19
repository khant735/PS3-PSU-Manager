#ifndef PS2_CBS_ARCHIVE_H
#define PS2_CBS_ARCHIVE_H
#include <stddef.h>
#include "ps2_cbs_entries.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    unsigned char *decoded;
    size_t decoded_length;
    ps2_cbs_entry *entries;
    size_t count;
    char directory[33];
} ps2_cbs_archive;
/* Read-only CBS decode + validated file enumeration. Caller owns result until release. */
int ps2_cbs_open(const unsigned char *bytes, size_t length, ps2_cbs_archive *result);
void ps2_cbs_close(ps2_cbs_archive *archive);
#ifdef __cplusplus
}
#endif
#endif
