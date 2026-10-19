#ifndef PS2_MAX_ENTRIES_H
#define PS2_MAX_ENTRIES_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { char name[33]; uint32_t size; size_t data_offset; } ps2_max_entry;
/* Parse the decompressed MAX stream, never writing to disk. MAX entries
   are 36-byte records (LE32 size, 32-byte name) with 16-byte aligned
   following entries, using Apollo's roundUp(offset+length+8,16)-8 rule.
   Returns 0 on success, -1 invalid arguments, -2 malformed data,
   -3 insufficient output entries. */
int ps2_max_list_entries(const unsigned char *data,size_t length,
                         uint32_t expected_count,ps2_max_entry *entries,
                         size_t capacity,size_t *count);
#ifdef __cplusplus
}
#endif
#endif
