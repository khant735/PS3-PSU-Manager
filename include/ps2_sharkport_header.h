#ifndef PS2_SHARKPORT_HEADER_H
#define PS2_SHARKPORT_HEADER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
 char title[256];
 char description[1024];
 char directory[65];
 unsigned int candidate_file_count;
 char candidate_files[16][33];
 /* Diagnostic record metadata; no file content is exported. */
 size_t candidate_offsets[16];
 size_t candidate_data_offsets[16];
 int record_chain_valid;
 unsigned int validated_file_count;
 int icon_sys_valid;
 uint32_t candidate_sizes[16];
 uint32_t payload_length;
 size_t payload_offset;
} ps2_sharkport_header;
/* Parse bounded outer metadata; payload remains untrusted and undecoded. */
int ps2_sharkport_convert_to_psu(const unsigned char *bytes, size_t length, const char *output_path);
int ps2_sharkport_read_header(const unsigned char *bytes, size_t length, ps2_sharkport_header *out);
#ifdef __cplusplus
}
#endif
#endif
