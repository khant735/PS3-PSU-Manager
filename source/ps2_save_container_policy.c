#include "ps2_save_container_policy.h"
#include <string.h>
ps2_container_scope ps2_container_get_scope(ps2_container_type type) {
    switch (type) {
        case PS2_CONTAINER_PSU: case PS2_CONTAINER_MAX: case PS2_CONTAINER_CBS:
        case PS2_CONTAINER_XPS_SPS: case PS2_CONTAINER_PSV: return PS2_CONTAINER_SINGLE_SAVE;
        case PS2_CONTAINER_RAW_CARD: return PS2_CONTAINER_FULL_CARD;
        default: return PS2_CONTAINER_INVALID;
    }
}
int ps2_container_is_raw_card_candidate(const unsigned char *data, size_t header_size, size_t file_size) {
    static const char magic[] = "Sony PS2 Memory Card Format ";
    /* Raw page data, without ECC/spare bytes. Do not infer directory count from capacity. */
    if (!data || header_size < sizeof(magic)-1 || file_size < 8192u*1024u || (file_size % (1024u*1024u)) != 0) return 0;
    return memcmp(data, magic, sizeof(magic)-1) == 0;
}
const char *ps2_container_export_extension(void) { return ".psu"; }
