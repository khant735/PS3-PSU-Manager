#include "ps2_max_archive.h"
#include "ps2_max_decode.h"
#include "ps2_save_archive_header.h"
#include <string.h>
int ps2_max_archive_list(const uint8_t *archive, size_t archive_size,
                         uint8_t *decoded_buffer, size_t decoded_capacity,
                         ps2_max_entry *entries, size_t entry_capacity,
                         size_t *decoded_size, size_t *entry_count) {
    ps2_archive_header header;
    size_t n = 0, count = 0;
    int rc;
    if (decoded_size) *decoded_size = 0;
    if (entry_count) *entry_count = 0;
    if (!archive || !decoded_buffer || !entries || !decoded_size || !entry_count)
        return -1;
    memset(&header, 0, sizeof(header));
    rc = ps2_archive_read_header(archive, archive_size, &header);
    if (rc || header.format != PS2_SAVE_MAX) return -2;
    if (!header.entry_count || header.entry_count > entry_capacity ||
        header.unpacked_bytes > decoded_capacity) return -3;
    rc = ps2_max_archive_decode(archive, archive_size, decoded_buffer,
                                decoded_capacity, &n);
    if (rc) return -4;
    rc = ps2_max_list_entries(decoded_buffer, n, header.entry_count,
                              entries, entry_capacity, &count);
    if (rc) return -5;
    *decoded_size = n;
    *entry_count = count;
    return 0;
}
