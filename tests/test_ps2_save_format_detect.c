#include "ps2_save_format_detect.h"
#include <assert.h>
#include <string.h>
int main(void) {
 unsigned char buf[32]={0};
 assert(ps2_save_detect_header(0,0)==PS2_SAVE_UNKNOWN);
 assert(ps2_save_detect_header(buf,32)==PS2_SAVE_UNKNOWN);
 memcpy(buf,"Ps2PowerSave",12);
 assert(ps2_save_detect_header(buf,11)==PS2_SAVE_UNKNOWN);
 assert(ps2_save_detect_header(buf,12)==PS2_SAVE_MAX);
 memset(buf,0,sizeof(buf)); memcpy(buf,"CFU\0",4);
 assert(ps2_save_detect_header(buf,3)==PS2_SAVE_UNKNOWN);
 assert(ps2_save_detect_header(buf,4)==PS2_SAVE_CBS);
 memset(buf,0,sizeof(buf)); memcpy(buf+4,"SharkPortSave\0\0\0",16);
 assert(ps2_save_detect_header(buf,19)==PS2_SAVE_UNKNOWN);
 assert(ps2_save_detect_header(buf,20)==PS2_SAVE_XPS_SPS);
 return 0;
}
