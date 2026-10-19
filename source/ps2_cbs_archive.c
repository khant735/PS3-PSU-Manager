#include "ps2_cbs_archive.h"
#include "ps2_cbs_decode.h"
#include "ps2_save_archive_header.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define CBS_MAX_DECODED (16u * 1024u * 1024u)
#define CBS_MAX_ENTRIES 4096u
void ps2_cbs_close(ps2_cbs_archive *a) {
    if (!a) return;
    free(a->entries);
    free(a->decoded);
    memset(a, 0, sizeof(*a));
}
int ps2_cbs_open(const unsigned char *bytes, size_t length, ps2_cbs_archive *result) {
    ps2_archive_header header;
    ps2_cbs_archive tmp;
    size_t written = 0, count = 0;
    int rc;
    if (!bytes || !result) return -1;
    memset(result, 0, sizeof(*result));
    memset(&tmp, 0, sizeof(tmp));
    if (ps2_archive_read_header(bytes, length, &header) || header.format != PS2_SAVE_CBS) return -2;
    if (!header.unpacked_bytes || header.unpacked_bytes > CBS_MAX_DECODED) return -3;
    tmp.decoded = (unsigned char *)malloc(header.unpacked_bytes);
    if (!tmp.decoded) return -4;
    rc = ps2_cbs_decode(bytes, length, tmp.decoded, header.unpacked_bytes, &written);
    if (rc || written != header.unpacked_bytes) { ps2_cbs_close(&tmp); return -5; }
    tmp.decoded_length = written;
    rc = ps2_cbs_list_entries(tmp.decoded, written, NULL, 0, &count);
    if ((rc != 0 && rc != -3) || !count || count > CBS_MAX_ENTRIES) { ps2_cbs_close(&tmp); return -6; }
    tmp.entries = (ps2_cbs_entry *)calloc(count, sizeof(*tmp.entries));
    if (!tmp.entries) { ps2_cbs_close(&tmp); return -4; }
    rc = ps2_cbs_list_entries(tmp.decoded, written, tmp.entries, count, &tmp.count);
    if (rc || tmp.count != count) { ps2_cbs_close(&tmp); return -6; }
    memcpy(tmp.directory, header.directory, sizeof(tmp.directory));
    *result = tmp;
    return 0;
}
