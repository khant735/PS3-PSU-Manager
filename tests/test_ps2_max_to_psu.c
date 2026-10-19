#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ps2_max_to_psu.h"
#include "ps2_max_archive.h"
#include "ps2_save_archive_header.h"
#include "psu.h"
/* Controlled archive-list fixture: verifies conversion and PSU writer only.
   This deliberately does NOT claim to test genuine MAX LZARI decoding. */
static const unsigned char fixture[]={0x41,0x42,0x43,0x44};
int ps2_archive_read_header(const unsigned char *a,size_t n,ps2_archive_header *h) {
 if(!a||n!=sizeof(fixture)||memcmp(a,fixture,n))return -1;
 memset(h,0,sizeof(*h));h->format=PS2_SAVE_MAX;
 strcpy(h->directory,"BASLUS-12345");h->unpacked_bytes=64;h->entry_count=1;return 0;
}
int ps2_max_archive_list(const uint8_t *a,size_t n,uint8_t *out,size_t cap,
 ps2_max_entry *e,size_t ec,size_t *sz,size_t *cnt) {
 if(n!=sizeof(fixture)||memcmp(a,fixture,n)||cap<64||ec<1)return -1;
 memset(out,0,64);memcpy(out+40,"TESTDATA",8);
 memset(e,0,sizeof(*e));strcpy(e[0].name,"icon.sys");e[0].size=8;e[0].data_offset=40;
 *sz=64;*cnt=1;return 0;
}
int main(void) {
 char dir[]="/tmp/maxpsutestXXXXXX",path[512];
 void *data=NULL;size_t size=0;
 assert(mkdtemp(dir));snprintf(path,sizeof(path),"%s/save.psu",dir);
 assert(ps2_max_convert_to_psu(fixture,sizeof(fixture),path)==0);
 assert(psu_read_file(path,"icon.sys",&data,&size)==0);
 assert(size==8 && memcmp(data,"TESTDATA",8)==0);free(data);
 assert(ps2_max_convert_to_psu(fixture,sizeof(fixture),path)!=0);
 unlink(path);rmdir(dir);puts("MAX->PSU conversion with mocked decoder: PASS");return 0;
}
