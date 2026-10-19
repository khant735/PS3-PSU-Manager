#include "ps2_xpo_v1.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
/* Lossless record export for forensic inspection only; never represents a PSU. */
int ps2_xpo_v1_export_record(const unsigned char *archive,size_t archive_size,const ps2_xpo_v1_info *info,unsigned index,const char *output_path){
 FILE *f;const ps2_xpo_v1_record *r;size_t written;
 if(!archive||!info||!output_path||index>=info->record_count||index>=PS2_XPO_V1_MAX_RECORDS)return -1;
 r=&info->records[index];
 if(r->payload_offset>archive_size||r->payload_size>archive_size-r->payload_offset)return -2;
 f=fopen(output_path,"wb");if(!f)return -3;
 written=fwrite(archive+r->payload_offset,1,r->payload_size,f);
 { int flush_rc=fflush(f); int close_rc=fclose(f); if(written!=r->payload_size||flush_rc!=0||close_rc!=0){remove(output_path);return -4;} }
 return 0;
}
