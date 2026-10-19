#include "ps2_archive_file_preview.h"
#include "ps2_cbs_archive.h"
#include "ps2_max_archive.h"
#include "ps2_save_archive_header.h"
#include "ps2_sharkport_header.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_ARCHIVE_BYTES (20u * 1024u * 1024u)
#define MAX_DECODED_BYTES (16u * 1024u * 1024u)
#define MAX_FILE_ENTRIES 4096u
int ps2_archive_preview_file(const char *path, ps2_archive_preview_callback cb, void *context) {
 FILE *f = NULL;
 unsigned char *raw = NULL, *decoded = NULL;
 ps2_max_entry *entries = NULL;
 ps2_archive_header hdr;
 long end;
 size_t count = 0, written = 0, n;
 int rc = -1;
 if (!path || !*path || !cb) return -1;
 f = fopen(path, "rb"); if (!f) return -2;
 if (fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) <= 0 ||
     (unsigned long)end > MAX_ARCHIVE_BYTES || fseek(f, 0, SEEK_SET) != 0) { rc=-3; goto done; }
 n = (size_t)end;
 raw = (unsigned char*)malloc(n); if (!raw) { rc=-4; goto done; }
 if (fread(raw,1,n,f) != n) { rc=-2; goto done; }
 fclose(f); f=NULL;
 /* SharkPort/X-Port: use validated record offsets, not the outer extension. */
 if (n>=21 && memcmp(raw+4,"SharkPortSave",13)==0) {
   ps2_sharkport_header sh;
   rc=ps2_sharkport_read_header(raw,n,&sh);
   if(rc || !sh.record_chain_valid) { rc=-5; goto done; }
   char view[65]={0};
   for(unsigned i=0;i<sh.candidate_file_count;i++) {
     if(strcmp(sh.candidate_files[i],"icon.sys") || sh.candidate_sizes[i]<452) continue;
     const unsigned char *sys=raw+sh.candidate_data_offsets[i];
     if(memcmp(sys,"PS2D",4)) continue;
     const unsigned char *ref=sys+260;
     size_t k=0;
     while(k<64 && ref[k] && ref[k]!='/' && ref[k]!='\\') k++;
     if(k && k<64) { memcpy(view,ref,k);view[k]=0; }
     break;
   }
   /* Never substitute Copy/Delete or an arbitrary archive icon for View.
      Missing/broken View references get the generic browser icon instead. */
   rc=0;
   if(view[0]) for(unsigned i=0;i<sh.candidate_file_count;i++) {
     const char *name=sh.candidate_files[i];
     if(strcmp(name,view))continue;
     size_t off=sh.candidate_data_offsets[i],sz=sh.candidate_sizes[i];
     if(off>n || sz>n-off) { rc=-5;break; }
     rc=cb(raw+off,sz,path,name,context)?1:-6;
     break;
   }
   goto done;
 }
 if (ps2_archive_read_header(raw,n,&hdr)) { rc=-5; goto done; }
 if (hdr.format == PS2_SAVE_CBS) {
   ps2_cbs_archive cbs;
   rc = ps2_cbs_open(raw,n,&cbs);
   if (rc) goto done;
   rc = ps2_archive_preview_cbs(cbs.decoded,cbs.decoded_length,cbs.entries,cbs.count,path,cb,context);
   ps2_cbs_close(&cbs);
 } else if (hdr.format == PS2_SAVE_MAX) {
   if (!hdr.unpacked_bytes || hdr.unpacked_bytes > MAX_DECODED_BYTES ||
       !hdr.entry_count || hdr.entry_count > MAX_FILE_ENTRIES) { rc=-3; goto done; }
   decoded=(unsigned char*)malloc(hdr.unpacked_bytes);
   entries=(ps2_max_entry*)calloc(hdr.entry_count,sizeof(*entries));
   if (!decoded || !entries) { rc=-4; goto done; }
   rc=ps2_max_archive_list(raw,n,decoded,hdr.unpacked_bytes,entries,hdr.entry_count,&written,&count);
   if (!rc) rc=ps2_archive_preview_max(decoded,written,entries,count,path,cb,context);
 } else rc=-5;
 done:
 if (f) fclose(f);
 free(entries); free(decoded); free(raw);
 return rc;
}
