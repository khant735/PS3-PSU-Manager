#include "ps2_save_archive_header.h"
#include <assert.h>
#include <string.h>
static void put32(unsigned char *p, unsigned v) {p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
int main(void) {
    unsigned char b[400]={0};ps2_archive_header r;
    assert(ps2_archive_read_header(0,0,&r)==-1);
    memcpy(b,"Ps2PowerSave",12);memcpy(b+16,"BASLUS-12345",12);memcpy(b+48,"icon.sys",9);
    put32(b+80,100);put32(b+84,3);put32(b+88,500);
    assert(ps2_archive_read_header(b,188,&r)==0 && r.format==PS2_SAVE_MAX && r.entry_count==3 && r.payload_offset==88);
    assert(ps2_archive_read_header(b,187,&r)==-2);
    b[16]='/';assert(ps2_archive_read_header(b,188,&r)==-2);b[16]='B';
    memset(b,0,sizeof(b));memcpy(b,"CFU\0",4);put32(b+8,296);put32(b+12,500);put32(b+16,42);memcpy(b+20,"BASLUS-12345",12);
    assert(ps2_archive_read_header(b,400,&r)==0 && r.format==PS2_SAVE_CBS && r.payload_offset==296);
    assert(ps2_archive_read_header(b,295,&r)==-2);
    put32(b+8,999);assert(ps2_archive_read_header(b,400,&r)==-2);
    return 0;
}
