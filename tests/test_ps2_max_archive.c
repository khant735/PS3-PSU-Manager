#include "ps2_max_archive.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 uint8_t input[128]={0},decoded[1024]={0};
 ps2_max_entry entries[4]; size_t written=99,count=99;
 assert(ps2_max_archive_list(NULL,0,decoded,sizeof(decoded),entries,4,&written,&count)==-1);
 assert(written==0 && count==0);
 assert(ps2_max_archive_list(input,sizeof(input),decoded,sizeof(decoded),entries,4,&written,&count)<0);
 assert(written==0 && count==0);
 puts("MAX archive decode-and-enumerate API rejection tests: PASS");
 return 0;
}
