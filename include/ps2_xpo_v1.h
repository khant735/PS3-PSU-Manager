#ifndef PS2_XPO_V1_H
#define PS2_XPO_V1_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PS2_XPO_V1_MAX_RECORDS 64
typedef struct {char name[65]; size_t header_offset,payload_offset,payload_size; unsigned char attribute_bytes[2]; unsigned char raw_metadata[184]; unsigned int metadata_word_46,metadata_word_4a; unsigned int attr_be; unsigned char created_raw[8],modified_raw[8]; char embedded_name[33]; int embedded_name_matches;} ps2_xpo_v1_record;
typedef struct {char directory[33];unsigned record_count;size_t payload_bytes;ps2_xpo_v1_record records[PS2_XPO_V1_MAX_RECORDS];} ps2_xpo_v1_info;
int ps2_xpo_v1_write_psu_with_icons(const unsigned char *archive,size_t archive_size,const ps2_xpo_v1_info *info,const char *output_path,const char *title,int generate_icons);
int ps2_xpo_v1_write_candidate_psu(const unsigned char *archive,size_t archive_size,const ps2_xpo_v1_info *info,const char *output_path);
int ps2_xpo_v1_export_record(const unsigned char *archive,size_t archive_size,const ps2_xpo_v1_info *info,unsigned index,const char *output_path);
/* Read-only parsing. No assumption that records map to PSU file entries. */
int ps2_xpo_v1_probe(const unsigned char *data,size_t size,ps2_xpo_v1_info *out);
#ifdef __cplusplus
}
#endif
#endif
