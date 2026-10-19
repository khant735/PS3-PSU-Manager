#include "ps2_sharkport_header.h"
#include <string.h>
#include <stdio.h>
static uint32_t u32(const unsigned char *p) {
 return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static int field(const unsigned char *b,size_t n,size_t *off,char *dest,size_t cap) {
 uint32_t len;
 if (*off>n || n-*off<4) return -1;
 len=u32(b+*off); *off+=4;
 if (len>=cap || len>n-*off) return -1;
 if (memchr(b+*off,0,len)) return -1;
 memcpy(dest,b+*off,len); dest[len]=0; *off+=len;
 return 0;
}
int ps2_sharkport_read_header(const unsigned char *b,size_t n,ps2_sharkport_header *out) {
 ps2_sharkport_header t; size_t off=21;
 if (!b || !out || n<33 || u32(b)!=13 || memcmp(b+4,"SharkPortSave\0\0\0",16)) return -1;
 memset(&t,0,sizeof(t));
 if (field(b,n,&off,t.title,sizeof(t.title)) || field(b,n,&off,t.description,sizeof(t.description))) return -2;
 if (!t.title[0] && !t.description[0] && n-off>=4) {
   /* ffgriever exports may use two empty strings followed by a note. */
   size_t trial=off;char note[1024];
   if(field(b,n,&trial,note,sizeof(note))==0 && trial+4<=n) {
     memcpy(t.description,note,strlen(note)+1);off=trial;
   }
 }
 if (n-off<4 || n-off>20u*1024u*1024u) return -2;
 /* Two observed layouts: a directory record at +4 or +8 after the
    metadata envelope. Some exporters prepend a length-prefixed text field.
    Validate all bounds before exposing a name; do not decode file data. */
 { size_t starts[2],k,j; int found=0;
   starts[0]=off;
   starts[1]=off;
   if(n-off>=4) {
     uint32_t extra=u32(b+off);
     if(extra>0 && extra<=4096 && extra<=n-off-4) starts[1]=off+4+extra;
   }
   for(k=0;k<2 && !found;k++) {
     size_t start=starts[k]; size_t delta;
     for(delta=4;delta<=8 && !found;delta+=4) {
       size_t name_at=start+delta+2;
       if(start>n || n-start<delta+66 || b[start+delta]!=0xfa || b[start+delta+1]!=0) continue;
       for(j=0;j<64;j++) {
         unsigned char c=b[name_at+j];
         if(!c) break;
         if(c<0x20 || c>0x7e || c=='/' || c==92) break;
       }
       if(j>0 && j<64 && b[name_at+j]==0) {
         memcpy(t.directory,b+name_at,j);t.directory[j]=0;found=1;
       }
     }
   }
 }
 /* Stage51: traverse file records by their declared lengths rather than
    scanning icon/texture payloads for coincidental FA 00 signatures.
    Exporters vary: directory size may describe an optional payload, and
    some regional saves have no trailing four-byte footer. */
 { size_t dirpos=0, first[2]; int have_dir=0, best=-1;
   for(size_t pos=off;pos+250<=n && pos<off+4096;pos++) {
     if(b[pos]!=0xfa || b[pos+1]!=0)continue;
     size_t j=0,k;int ok=1;
     while(j<64 && b[pos+2+j] && b[pos+2+j]>=32 && b[pos+2+j]<=126 &&
           b[pos+2+j]!='/' && b[pos+2+j]!=92)j++;
     if(!j || j>32 || j==64)continue;
     for(k=j;k<64;k++)if(b[pos+2+k]){ok=0;break;}
     if(!ok)continue;
     dirpos=pos;have_dir=1;break;
   }
   if(have_dir){
     uint32_t ds=u32(b+dirpos+66);
     first[0]=dirpos+250;
     first[1]=(size_t)ds<=n-first[0]?first[0]+ds:n;
     for(int variant=0;variant<2;variant++){
       size_t pos=first[variant];unsigned count=0;int ok=1;
       char names[16][33];size_t offsets[16],dataoffs[16];uint32_t sizes[16];
       while(pos<n && n-pos>4){
         size_t j=0,k;uint32_t sz;
         if(count>=16 || n-pos<250 || b[pos]!=0xfa || b[pos+1]!=0){ok=0;break;}
         while(j<64 && b[pos+2+j] && b[pos+2+j]>=32 && b[pos+2+j]<=126 &&
               b[pos+2+j]!='/' && b[pos+2+j]!=92)j++;
         if(!j || j>32 || j==64){ok=0;break;}
         for(k=j;k<64;k++)if(b[pos+2+k]){ok=0;break;}
         if(!ok)break;
         sz=u32(b+pos+66);
         if((size_t)sz>n-pos-250){ok=0;break;}
         memcpy(names[count],b+pos+2,j);names[count][j]=0;
         offsets[count]=pos;dataoffs[count]=pos+250;sizes[count]=sz;
         count++;pos+=250+(size_t)sz;
       }
       if(ok && count && (pos==n || pos==n-4) && (int)count>best){
         best=(int)count;t.candidate_file_count=count;t.validated_file_count=count;
         t.record_chain_valid=1;
         for(unsigned i=0;i<count;i++){
           strcpy(t.candidate_files[i],names[i]);
           t.candidate_offsets[i]=offsets[i];t.candidate_data_offsets[i]=dataoffs[i];
           t.candidate_sizes[i]=sizes[i];
           if(strcmp(names[i],"icon.sys")==0 && sizes[i]>=4 &&
              memcmp(b+dataoffs[i],"PS2D",4)==0)t.icon_sys_valid=1;
         }
       }
     }
   }
 }
 t.payload_offset=off;
 t.payload_length=(uint32_t)(n-off);
 *out=t; return 0;
}

/* Convert validated SharkPort records into the existing PSU view format.
   Uses generated timestamps because exporter timestamp layout is not yet verified. */
static void put16(unsigned char *p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void put32(unsigned char *p,unsigned v){for(int i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static int psu_record(FILE *f,const char *name,unsigned attr,uint32_t size){
 unsigned char h[512]={0};size_t len=strlen(name);if(len>32)return -1;
 put16(h,attr);put32(h+4,size);memcpy(h+64,name,len);
 return fwrite(h,1,sizeof(h),f)==sizeof(h)?0:-1;
}
int ps2_sharkport_convert_to_psu(const unsigned char *b,size_t n,const char *path){
 ps2_sharkport_header h;FILE *f=NULL;int rc=-1;
 if(!path||ps2_sharkport_read_header(b,n,&h)||!h.record_chain_valid||!h.validated_file_count||
    h.validated_file_count!=h.candidate_file_count||!h.directory[0]||strlen(h.directory)>32)return -1;
 f=fopen(path,"wb");if(!f)return -2;
 if(psu_record(f,h.directory,0x8427,h.candidate_file_count+2)||
    psu_record(f,".",0x8427,0)||psu_record(f,"..",0x8427,0))goto done;
 for(unsigned i=0;i<h.candidate_file_count;i++){
   size_t offset=h.candidate_data_offsets[i],size=h.candidate_sizes[i];
   static const unsigned char zero[1024]={0};size_t pad=(1024-(size%1024))%1024;
   if(offset>n||size>n-offset||psu_record(f,h.candidate_files[i],0x8497,(uint32_t)size))goto done;
   if(fwrite(b+offset,1,size,f)!=size || (pad && fwrite(zero,1,pad,f)!=pad))goto done;
 }
 rc=0;
 done:if(fclose(f)!=0)rc=-3;if(rc)remove(path);return rc;
}
