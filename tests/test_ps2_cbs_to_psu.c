#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <zlib.h>
#include "ps2_cbs_to_psu.h"
#include "psu.h"
#include "ps2_cbs_archive.h"
#include "../source/ps2_cbs_decode.c"
static void le32(unsigned char *p,unsigned n){p[0]=n;p[1]=n>>8;p[2]=n>>16;p[3]=n>>24;}
int main(void){
    unsigned char plain[160]={0}, archive[1024]={0}, compressed[512];
    unsigned char payload[]={1,2,3,4,5,6,7,8};
    uLongf clen=sizeof(compressed);
    void *out=NULL;size_t sz=0;
    char dir[]="/tmp/cbspsutestXXXXXX", path[512];
    assert(mkdtemp(dir));
    snprintf(path,sizeof(path),"%s/test.psu",dir);
    plain[0]=0;plain[1]=12;plain[2]=34;plain[3]=5;plain[4]=6;plain[5]=7;plain[6]=0xEA;plain[7]=7;
    memcpy(plain+8,plain,8);plain[9]=56;
    le32(plain+16,sizeof(payload));le32(plain+20,0x8497);
    memcpy(plain+32,"icon.sys",8);memcpy(plain+64,payload,sizeof(payload));
    assert(compress2(compressed,&clen,plain,64+sizeof(payload),9)==Z_OK);
    memcpy(archive,"CFU\0",4);le32(archive+8,296);le32(archive+12,64+sizeof(payload));
    memcpy(archive+20,"BASLUS-99999",12);
    memcpy(archive+52,plain,8); memcpy(archive+60,plain+8,8);
    le32(archive+72,0x8427);
    memcpy(archive+296,compressed,clen);cbs_crypt(archive+296,clen);
    assert(ps2_cbs_convert_to_psu(archive,296+clen,path)==0);
    assert(psu_read_file(path,"icon.sys",&out,&sz)==0);
    assert(sz==sizeof(payload) && memcmp(out,payload,sz)==0);free(out);
    { FILE *f=fopen(path,"rb"); unsigned char hdr[512];assert(f);
      assert(fread(hdr,1,512,f)==512);
      assert(memcmp(hdr+8,archive+52,8)==0);
      assert(memcmp(hdr+24,archive+60,8)==0);
      assert(hdr[0]==0x27 && hdr[1]==0x84);
      assert(fseek(f,3*512,SEEK_SET)==0);assert(fread(hdr,1,512,f)==512);fclose(f);
      assert(memcmp(hdr+8,plain,8)==0);assert(memcmp(hdr+24,plain+8,8)==0); }
    assert(ps2_cbs_convert_to_psu(archive,296+clen,path)!=0);
    unlink(path);rmdir(dir);
    puts("CBS->PSU conversion synthetic fixture: PASS");
    return 0;
}
