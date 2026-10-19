#include "ps2_xpo_v1.h"
#include <string.h>
static unsigned rd32(const unsigned char *p){return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
int ps2_xpo_v1_probe(const unsigned char *b,size_t n,ps2_xpo_v1_info *out){
 size_t pos;unsigned i,count;
 if(!b||!out||n<552||rd32(b)!=1||b[4]!=0x2a||b[5]!=1||rd32(b+8)!=n-4)return -1;
 count=(unsigned)b[6]|((unsigned)b[7]<<8);
 if(count==0||count>64)return -2;
 memset(out,0,sizeof(*out));pos=302;
 for(i=0;i<count;i++){
  size_t j;unsigned size;
  if(pos>n||n-pos<250||b[pos]!=0xfa||b[pos+1]!=0)return -3;
  for(j=0;j<64&&b[pos+2+j];j++)if(b[pos+2+j]<32||b[pos+2+j]>126||b[pos+2+j]=='/'||b[pos+2+j]=='\\')return -4;
  if(j==0||j>32||j==64)return -4;
  for(size_t k=j;k<64;k++)if(b[pos+2+k])return -4;
  size=rd32(b+pos+66);
  if(size>n-pos-250)return -5;
  if(i==0){memcpy(out->directory,b+pos+2,j);out->directory[j]=0;}
  memcpy(out->records[i].name,b+pos+2,j);out->records[i].name[j]=0;
  out->records[i].header_offset=pos;out->records[i].payload_offset=pos+250;
  out->records[i].payload_size=size;
  out->records[i].attribute_bytes[0]=b[pos+78];out->records[i].attribute_bytes[1]=b[pos+79];
  /* Preserve opaque X-Port v1 metadata byte-for-byte; avoid unverified PSU mapping. */
  memcpy(out->records[i].raw_metadata,b+pos+66,184);
  out->records[i].metadata_word_46=rd32(b+pos+70);
  out->records[i].metadata_word_4a=rd32(b+pos+74);
  out->records[i].attr_be=((unsigned)b[pos+78]<<8)|b[pos+79];
  memcpy(out->records[i].created_raw,b+pos+82,8);
  memcpy(out->records[i].modified_raw,b+pos+90,8);
  { size_t m=0; const unsigned char *q=b+pos+115;
    while(m<32 && q[m]>=32 && q[m]<=126 && q[m]!='/' && q[m]!='\\')m++;
    memcpy(out->records[i].embedded_name,q,m);out->records[i].embedded_name[m]=0;
    out->records[i].embedded_name_matches=(m==j && memcmp(q,b+pos+2,j)==0);
  }

  out->record_count++;pos+=250+(size_t)size;
 }
 if(pos!=n)return -6;
 out->payload_bytes=n-302-250*(size_t)count;
 return 0;
}
