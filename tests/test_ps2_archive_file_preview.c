#include "ps2_archive_file_preview.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int callback(const void *data,size_t len,const char *archive,const char *entry,void *ctx){(void)data;(void)len;(void)archive;(void)entry;(void)ctx;return 1;}
int main(void){
 const char *name="stage22_bad_archive.bin";
 FILE *f;
 assert(ps2_archive_preview_file(NULL,callback,NULL)<0);
 assert(ps2_archive_preview_file("stage22_nonexistent.bin",callback,NULL)<0);
 assert(ps2_archive_preview_file(name,NULL,NULL)<0);
 f=fopen(name,"wb");assert(f);assert(fwrite("bad",1,3,f)==3);assert(!fclose(f));
 assert(ps2_archive_preview_file(name,callback,NULL)<0);
 remove(name);
 puts("Stage22 file preview negative tests passed");return 0;
}
