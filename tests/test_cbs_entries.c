#include "ps2_cbs_entries.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void le32(unsigned char *p,unsigned v) {p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
int main(void) {
 unsigned char b[64+5+64+3]={0}; ps2_cbs_entry entries[2];size_t n=99;
 le32(b+16,5);le32(b+20,0x8497);memcpy(b+32,"icon.sys",9);memcpy(b+64,"hello",5);
 le32(b+69+16,3);memcpy(b+69+32,"view.icn",9);memcpy(b+69+64,"xyz",3);
 assert(ps2_cbs_list_entries(b,sizeof(b),entries,2,&n)==0 && n==2);
 assert(!strcmp(entries[0].name,"icon.sys") && entries[0].data_offset==64 && entries[0].size==5);
 assert(!strcmp(entries[1].name,"view.icn") && entries[1].data_offset==133 && entries[1].size==3);
 assert(ps2_cbs_list_entries(b,sizeof(b),entries,1,&n)==-3 && n==2);
 assert(ps2_cbs_list_entries(b,sizeof(b)-1,entries,2,&n)==-2);
 b[32]='/';assert(ps2_cbs_list_entries(b,sizeof(b),entries,2,&n)==-2);b[32]='i';
 le32(b+16,0xffffffffu);assert(ps2_cbs_list_entries(b,sizeof(b),entries,2,&n)==-2);
 puts("CBS decoded-entry parser: PASS");return 0;
}
