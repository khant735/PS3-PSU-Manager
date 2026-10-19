#include "ps2_max_entries.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 unsigned char buf[128]={0}; ps2_max_entry entries[2];size_t n=99;
 buf[0]=3;memcpy(buf+4,"icon.sys",9);memcpy(buf+36,"ABC",3);
 /* 36+3 -> roundUp(39+8,16)-8 = 40 */
 buf[40]=4;memcpy(buf+44,"view.icn",9);memcpy(buf+76,"DATA",4);
 assert(ps2_max_list_entries(buf,80,2,entries,2,&n)==0);
 assert(n==2 && entries[0].data_offset==36 && entries[1].data_offset==76);
 assert(entries[0].size==3 && entries[1].size==4);
 assert(memcmp(buf+entries[0].data_offset,"ABC",3)==0);
 assert(memcmp(buf+entries[1].data_offset,"DATA",4)==0);
 assert(ps2_max_list_entries(buf,79,2,entries,2,&n)<0);
 assert(ps2_max_list_entries(buf,80,2,entries,1,&n)==-3);
 buf[44]='/';assert(ps2_max_list_entries(buf,80,2,entries,2,&n)<0);buf[44]='v';
 memcpy(buf+44,"icon.sys",9);assert(ps2_max_list_entries(buf,80,2,entries,2,&n)<0);
 puts("MAX two-entry read-only parsing and malformed input: PASS");return 0;
}
