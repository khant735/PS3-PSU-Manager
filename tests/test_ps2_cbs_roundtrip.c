/* Synthetic CBS positive-path fixture: compressed zlib payload, then CBS RC4 permutation. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
/* Internal test-only inclusion permits verifying encryption is inverse of decoding. */
#include "../source/ps2_cbs_decode.c"
int main(void) {
 static const unsigned char plain[]="PS2 SAVE TEST: icon.sys and data.bin synthetic bytes\0\x01\x02";
 unsigned char archive[2048]={0},out[256]={0},compressed[1024];
 uLongf clen=sizeof(compressed);size_t written=0,n;
 assert(compress2(compressed,&clen,plain,sizeof(plain),Z_BEST_COMPRESSION)==Z_OK);
 memcpy(archive,"CFU\0",4);
 archive[8]=296&255;archive[9]=296>>8;
 archive[12]=(unsigned char)sizeof(plain);
 memcpy(archive+20,"BASLUS-99999",12);
 memcpy(archive+296,compressed,clen);
 cbs_crypt(archive+296,clen);
 n=296+(size_t)clen;
 assert(ps2_cbs_decode(archive,n,out,sizeof(out),&written)==0);
 assert(written==sizeof(plain));assert(!memcmp(out,plain,sizeof(plain)));
 assert(ps2_cbs_decode(archive,n-3,out,sizeof(out),&written)!=0);
 assert(ps2_cbs_decode(archive,n,out,4,&written)!=0);
 archive[0]='X';assert(ps2_cbs_decode(archive,n,out,sizeof(out),&written)!=0);
 puts("CBS synthetic encrypted + compressed positive roundtrip: PASS");
 return 0;
}
