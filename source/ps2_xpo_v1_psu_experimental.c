/* Experimental X-Port v1 to PSU writer. Not game-compatibility verified. */
#include "ps2_xpo_v1.h"
#include "psu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void le16(unsigned char *p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void le32(unsigned char *p,unsigned v){for(int i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static int entry(FILE *f,const char *name,unsigned attr,unsigned size,const unsigned char *ct,const unsigned char *mt){
 unsigned char h[512]={0};size_t n=strlen(name);if(n>32)return -1;
 le16(h,attr);le32(h+4,size);if(ct)memcpy(h+8,ct,8);if(mt)memcpy(h+24,mt,8);
 memcpy(h+64,name,n);return fwrite(h,1,sizeof(h),f)==sizeof(h)?0:-1;
}
int ps2_xpo_v1_write_psu_with_icons(const unsigned char *b,size_t n,const ps2_xpo_v1_info *info,const char *dest,const char *title,int generate_icons){
 FILE *f=NULL;unsigned char pad[1024]={0};int rc=-1;
 const char *names[]={"icon.sys","icon.icn","copy.icn","del.icn"};
 void *icon_data[4]={0};size_t icon_sizes[4]={0};unsigned added=0;char temp[1200]={0};
 if(!b||!info||!dest||!info->record_count||info->record_count>PS2_XPO_V1_MAX_RECORDS||n<302)return -1;
 if(generate_icons){
  if(!title||!*title)return -5;
  for(unsigned j=0;j<info->record_count;j++)for(unsigned k=0;k<4;k++)if(!strcmp(info->records[j].name,names[k]))return -6;
  if(snprintf(temp,sizeof(temp),"%s.icontmp",dest)>=(int)sizeof(temp))return -7;
  FILE *check=fopen(temp,"rb");if(check){fclose(check);return -8;}
  int err=psu_create_blank_template_ex2(temp,info->directory,title,"",title,"icon");
  if(err)return -9;
  for(unsigned k=0;k<4;k++)if(psu_read_file(temp,names[k],&icon_data[k],&icon_sizes[k])||icon_sizes[k]>UINT32_MAX){rc=-10;goto cleanup;}
  added=4;
 }
 /* Never overwrite a pre-existing file. Caller must use a fresh output name. */
 if((f=fopen(dest,"rb"))){fclose(f);rc=-2;goto cleanup;}
 f=fopen(dest,"wb");if(!f){rc=-3;goto cleanup;}
 if(entry(f,info->directory,0x8427,info->record_count+2+added,b+0x86,b+0x8e))goto done;
 if(entry(f,".",0x8427,0,NULL,NULL)||entry(f,"..",0x8427,0,NULL,NULL))goto done;
 for(unsigned i=0;i<info->record_count;i++){
  const ps2_xpo_v1_record *r=&info->records[i];
  if(r->payload_offset>n||r->payload_size>n-r->payload_offset||r->payload_size>UINT32_MAX)goto done;
  if(entry(f,r->name,0x8497,(unsigned)r->payload_size,r->created_raw,r->modified_raw))goto done;
  if(fwrite(b+r->payload_offset,1,r->payload_size,f)!=r->payload_size)goto done;
  size_t z=(1024-r->payload_size%1024)%1024;
  if(z&&fwrite(pad,1,z,f)!=z)goto done;
 }
 for(unsigned k=0;k<added;k++){
  if(entry(f,names[k],0x8497,(unsigned)icon_sizes[k],NULL,NULL))goto done;
  if(fwrite(icon_data[k],1,icon_sizes[k],f)!=icon_sizes[k])goto done;
  size_t z=(1024-icon_sizes[k]%1024)%1024;
  if(z&&fwrite(pad,1,z,f)!=z)goto done;
 }
 rc=0;
 done:if(fclose(f)!=0)rc=-4;if(rc)remove(dest);
 cleanup:for(unsigned k=0;k<4;k++)free(icon_data[k]);if(temp[0])remove(temp);return rc;
}
int ps2_xpo_v1_write_candidate_psu(const unsigned char *b,size_t n,const ps2_xpo_v1_info *info,const char *dest){
 return ps2_xpo_v1_write_psu_with_icons(b,n,info,dest,NULL,0);
}
