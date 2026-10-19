#include "ps2_max_decode.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(void){uint8_t b[128]={0},out[32]={0};size_t written=999;
 assert(ps2_max_lzari_decode(NULL,0,out,sizeof(out),&written)<0);
 assert(ps2_max_lzari_decode(b,3,out,sizeof(out),&written)<0);
 b[0]=100;assert(ps2_max_lzari_decode(b,sizeof(b),out,sizeof(out),&written)<0);
 assert(ps2_max_archive_decode(b,sizeof(b),out,sizeof(out),&written)<0);
 puts("MAX malformed input / capacity checks: PASS");return 0;}
