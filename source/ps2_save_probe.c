#include "ps2_save_probe.h"
#include <stdio.h>
#include <string.h>
int ps2_save_probe_file(const char *path, ps2_save_probe_result *result) {
    unsigned char header[64];
    size_t count;
    FILE *f;
    long end;
    if (!path || !result) return -1;
    memset(result, 0, sizeof(*result));
    f = fopen(path, "rb");
    if (!f) return -2;
    if (fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f); return -2;
    }
    result->file_size = (uint64_t)(unsigned long)end;
    count = fread(header, 1, sizeof(header), f);
    if (ferror(f)) { fclose(f); return -2; }
    fclose(f);
    result->format = ps2_save_detect_header(header, count);
    result->signature_valid = result->format != PS2_SAVE_UNKNOWN;
    /* Presence is not proof of decodability or archive integrity. */
    result->payload_present = result->signature_valid &&
        result->file_size > (result->format == PS2_SAVE_XPS_SPS ? 20u :
                             result->format == PS2_SAVE_MAX ? 12u : 4u);
    result->supported_decoder = 0;
    return 0;
}
