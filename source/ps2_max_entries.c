#include "ps2_max_entries.h"
#include <string.h>
static uint32_t le32(const unsigned char *p) {
 return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static int name_ok(const unsigned char *p,char *out) {
 size_t i;
 for(i=0;i<32 && p[i];i++) {
  if(p[i]<32 || p[i]==127 || p[i]=='/' || p[i]=='\\' || p[i]==':')return 0;
  out[i]=(char)p[i];
 }
 if(i==0 || i==32)return 0;
 out[i]=0;
 return strcmp(out,".")!=0 && strcmp(out,"..")!=0;
}
int ps2_max_list_entries(const unsigned char *data,size_t length,
 uint32_t expected_count,ps2_max_entry *entries,size_t capacity,size_t *count) {
 size_t pos=0,i,j;
 if(count)*count=0;
 if(!data || !entries || !count || !expected_count || expected_count>65535)return -1;
 if(expected_count>capacity)return -3;
 for(i=0;i<expected_count;i++) {
  ps2_max_entry e;
  size_t end,next;
  if(pos>length || length-pos<36)return -2;
  memset(&e,0,sizeof(e));
  e.size=le32(data+pos);
  if(!name_ok(data+pos+4,e.name))return -2;
  for(j=0;j<i;j++)if(strcmp(entries[j].name,e.name)==0)return -2;
  e.data_offset=pos+36;
  if((size_t)e.size>length-e.data_offset)return -2;
  end=e.data_offset+(size_t)e.size;
  entries[i]=e;
  if(i+1<expected_count) {
   /* Apollo's alignment: roundUp(end+8,16)-8; guard overflow. */
   if(end>SIZE_MAX-23)return -2;
   next=((end+23u)&~(size_t)15u)-8u;
   if(next>end && next>length)return -2;
   pos=next;
  }
 }
 *count=expected_count;
 return 0;
}
