#include "ps2_save_container_policy.h"
#include <assert.h>
#include <string.h>
int main(void) {
    unsigned char header[64] = {0};
    assert(ps2_container_get_scope(PS2_CONTAINER_MAX) == PS2_CONTAINER_SINGLE_SAVE);
    assert(ps2_container_get_scope(PS2_CONTAINER_CBS) == PS2_CONTAINER_SINGLE_SAVE);
    assert(ps2_container_get_scope(PS2_CONTAINER_XPS_SPS) == PS2_CONTAINER_SINGLE_SAVE);
    assert(ps2_container_get_scope(PS2_CONTAINER_PSV) == PS2_CONTAINER_SINGLE_SAVE);
    assert(ps2_container_get_scope(PS2_CONTAINER_PSU) == PS2_CONTAINER_SINGLE_SAVE);
    assert(ps2_container_get_scope(PS2_CONTAINER_RAW_CARD) == PS2_CONTAINER_FULL_CARD);
    assert(ps2_container_get_scope(PS2_CONTAINER_UNKNOWN) == PS2_CONTAINER_INVALID);
    assert(strcmp(ps2_container_export_extension(), ".psu") == 0);
    memcpy(header, "Sony PS2 Memory Card Format ", sizeof("Sony PS2 Memory Card Format ")-1);
    assert(ps2_container_is_raw_card_candidate(header, sizeof(header), 8u*1024u*1024u));
    assert(ps2_container_is_raw_card_candidate(header, sizeof(header), 16u*1024u*1024u));
    assert(!ps2_container_is_raw_card_candidate(header, sizeof(header), 1024));
    assert(!ps2_container_is_raw_card_candidate(header, 10, 8u*1024u*1024u));
    header[0]='X';
    assert(!ps2_container_is_raw_card_candidate(header, sizeof(header), 8u*1024u*1024u));
    return 0;
}
