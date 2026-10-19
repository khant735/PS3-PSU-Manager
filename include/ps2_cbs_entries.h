#ifndef PS2_CBS_ENTRIES_H
#define PS2_CBS_ENTRIES_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    char name[33];
    uint32_t size;
    uint32_t mode;
    unsigned char created[8];
    unsigned char modified[8];
    size_t data_offset;
} ps2_cbs_entry;
/* Validate and enumerate decoded CBS entries. The returned offsets refer to
   the caller-owned decoded buffer. Never writes to the filesystem.
   0 success, -1 invalid args, -2 malformed input, -3 insufficient entries. */
int ps2_cbs_list_entries(const unsigned char *decoded, size_t length,
                         ps2_cbs_entry *entries, size_t capacity, size_t *count);
#ifdef __cplusplus
}
#endif
#endif
