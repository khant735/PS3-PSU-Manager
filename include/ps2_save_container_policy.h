#ifndef PS2_SAVE_CONTAINER_POLICY_H
#define PS2_SAVE_CONTAINER_POLICY_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { PS2_CONTAINER_UNKNOWN=0, PS2_CONTAINER_PSU, PS2_CONTAINER_MAX, PS2_CONTAINER_CBS, PS2_CONTAINER_XPS_SPS, PS2_CONTAINER_PSV, PS2_CONTAINER_RAW_CARD } ps2_container_type;
typedef enum { PS2_CONTAINER_INVALID=0, PS2_CONTAINER_SINGLE_SAVE, PS2_CONTAINER_FULL_CARD } ps2_container_scope;
/* Policy describes format structure, NOT a claim that a decoder is implemented. */
ps2_container_scope ps2_container_get_scope(ps2_container_type type);
/* A raw PS2 card header can identify a candidate card image; it does not validate its filesystem. */
int ps2_container_is_raw_card_candidate(const unsigned char *data, size_t header_size, size_t file_size);
/* No other output format is permitted. */
const char *ps2_container_export_extension(void);
#ifdef __cplusplus
}
#endif
#endif
