#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>
#include "ps2_cbs_archive.h"
/* Test-only CBS encryption is deliberately independent of the production API. */
#include "../source/ps2_cbs_decode.c"
static void le32(unsigned char *p, unsigned n) { p[0]=n; p[1]=n>>8; p[2]=n>>16; p[3]=n>>24; }
int main(void) {
    unsigned char decoded[256]={0}, archive[1024]={0}, compressed[512];
    const char *name="icon.sys";
    const unsigned char payload[]={0x50,0x53,0x32,0,0xff};
    size_t dlen=64+sizeof(payload), alen;
    uLongf clen=sizeof(compressed);
    ps2_cbs_archive result;
    le32(decoded+16,sizeof(payload)); le32(decoded+20,0x8497);
    memcpy(decoded+32,name,strlen(name)); memcpy(decoded+64,payload,sizeof(payload));
    assert(compress2(compressed,&clen,decoded,dlen,Z_BEST_COMPRESSION)==Z_OK);
    memcpy(archive,"CFU\0",4);le32(archive+8,296);le32(archive+12,dlen);
    memcpy(archive+20,"BASLUS-99999",12);
    memcpy(archive+296,compressed,clen);cbs_crypt(archive+296,clen);
    alen=296+clen;
    assert(ps2_cbs_open(archive,alen,&result)==0);
    assert(result.count==1 && strcmp(result.directory,"BASLUS-99999")==0);
    assert(strcmp(result.entries[0].name,name)==0);
    assert(result.entries[0].size==sizeof(payload));
    assert(!memcmp(result.decoded+result.entries[0].data_offset,payload,sizeof(payload)));
    ps2_cbs_close(&result);
    assert(ps2_cbs_open(archive,alen-2,&result)!=0);
    archive[0]='X'; assert(ps2_cbs_open(archive,alen,&result)!=0);
    puts("CBS full read-only pipeline synthetic fixture: PASS");
    return 0;
}
