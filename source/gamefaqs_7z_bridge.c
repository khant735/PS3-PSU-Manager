#include "gamefaqs_7z_bridge.h"
#include <string.h>
/* Rows are validated by the application's existing URL/format policy.
   Never interpret the cache as a PS2 save archive or convert it to PSU. */
int gfs_7z_import_line(const char *line, size_t length, void *context) {
  gfs_7z_import_context *ctx=(gfs_7z_import_context *)context;
  char row[1800];
  gfs_save_t entry;
  int parsed;
  if(!ctx||!ctx->on_entry||!line)return -1;
  if(length==0||line[0]=='#')return 0;
  if(length>=sizeof(row)-1||memchr(line,0,length)){
    ctx->rejected++;
    return -1;
  }
  memcpy(row,line,length);
  row[length]='\n';
  row[length+1]=0;
  memset(&entry,0,sizeof(entry));
  parsed=gfs_parse_index(row,&entry,1);
  if(parsed!=1){
    ctx->rejected++;
    return -1;
  }
  if(ctx->on_entry(&entry,ctx->user))return -1;
  ctx->accepted++;
  return 0;
}
