#include "ps2_cbs_decode.h"
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>
/* Negative-path tests only: authentic CBS fixtures are still required. */
int main(void) {
    unsigned char buf[400]={0},out[100]={0}; size_t n=99;
    assert(ps2_cbs_decode(NULL,sizeof(buf),out,sizeof(out),&n)!=0 && n==0);
    memcpy(buf,"CFU",3);buf[8]=0x28;buf[9]=1; /* 296-byte header */
    memcpy(buf+20,"BASLUS-12345",12);buf[12]=20;buf[16]=1;
    assert(ps2_cbs_decode(buf,sizeof(buf),out,sizeof(out),&n)!=0 && n==0);
    assert(ps2_cbs_decode(buf,12,out,sizeof(out),&n)!=0);
    return 0;
}
