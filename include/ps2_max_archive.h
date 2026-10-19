#ifndef PS2_MAX_ARCHIVE_H
#define PS2_MAX_ARCHIVE_H
#include <stddef.h>
#include <stdint.h>
#include "ps2_max_entries.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Read-only MAX archive processing. The caller owns the decoded buffer and
   entry array; returned entry offsets refer to decoded_buffer. Never writes
   to the filesystem and does not convert between extra archive formats.
   Return 0 on success; negative on malformed input or inadequate capacity. */
int ps2_max_archive_list(const uint8_t *archive, size_t archive_size,
                         uint8_t *decoded_buffer, size_t decoded_capacity,
                         ps2_max_entry *entries, size_t entry_capacity,
                         size_t *decoded_size, size_t *entry_count);
#ifdef __cplusplus
}
#endif
#endif
