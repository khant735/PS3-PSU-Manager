#include "gamefaqs_index_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

static int clean(const char *s,size_t cap){
 if(!s||strlen(s)>=cap)return 0;
 for(;*s;s++)if(*s=='\t'||*s=='\r'||*s=='\n')return 0;
 return 1;
}
static int valid(const gfs_save_t *e){
 unsigned game_id=0,save_id=0;
 if(!gfs_parse_save_page_ids(e->page_url,&game_id,&save_id))return 0;
 /* PAGE records without a save ID represent listing seeds. */
 if(!save_id&&(strcmp(e->format,"PAGE")||e->download_url[0]))return 0;
 return clean(e->game,sizeof(e->game))&&*e->game&&
 clean(e->region,sizeof(e->region))&&*e->region&&
 clean(e->format,sizeof(e->format))&&*e->format&&
 clean(e->description,sizeof(e->description))&&
 clean(e->page_url,sizeof(e->page_url))&&
 clean(e->download_url,sizeof(e->download_url))&&
 gfs_parse_save_page_ids(e->page_url,&(unsigned){0},&(unsigned){0})&&
 (!*e->download_url||gfs_download_allowed(e->download_url))&&
 (strcmp(e->format,"PAGE")||!*e->download_url);
}
static int same(const gfs_save_t *a,const gfs_save_t *b){
 return !strcmp(a->page_url,b->page_url)&&!strcmp(a->download_url,b->download_url);
}
int gfs_merge_index_file(const char *path,const gfs_save_t *incoming,int incoming_count,
                         int maximum_entries){
 if(!path||!*path||incoming_count<0||(incoming_count&&!incoming)||
    maximum_entries<1||maximum_entries>GFS_MAX_ENTRIES)return -1;
 size_t plen=strlen(path);
 if(plen>900)return -1;
 char *tmp=(char*)malloc(plen+32);
 if(!tmp)return -1;
 snprintf(tmp,plen+32,"%s.tmp",path);
 /* Bound old file size and keep its bytes intact if parsing is incomplete. */
 FILE *f=fopen(path,"rb");long bytes=0;
 if(f){
  if(fseek(f,0,SEEK_END)!=0||(bytes=ftell(f))<0||bytes>maximum_entries*1800L||
     fseek(f,0,SEEK_SET)!=0){fclose(f);free(tmp);return -1;}
 }else if(errno!=ENOENT){free(tmp);return -1;}
 char *data=(char*)malloc((size_t)bytes+1);
 if(!data){if(f)fclose(f);free(tmp);return -1;}
 if(f){
  if(fread(data,1,(size_t)bytes,f)!=(size_t)bytes){fclose(f);free(data);free(tmp);return -1;}
  fclose(f);
 }
 data[bytes]=0;
 /* Refuse to overwrite a database containing rows the current parser
  * cannot round-trip. An unknown or malformed row is not an empty save. */
 int expected=0;
 if(bytes && memchr(data,0,(size_t)bytes)){
  free(data);free(tmp);return -1;
 }
 for(const char *line=data;*line;){
  const char *end=strchr(line,'\n');
  if(!end)end=line+strlen(line);
  const char *p=line;
  while(p<end&&(*p==' '||*p=='\r'))p++;
  if(p<end&&*p!='#')expected++;
  line=*end?end+1:end;
 }
 if(expected>maximum_entries||incoming_count>maximum_entries-expected){
  free(data);free(tmp);return -1;
 }
 /* Allocate for existing records and this merge, not the 8192-entry ceiling.
  * Keep one slot for empty files because calloc(0) is implementation-defined. */
 size_t capacity=(size_t)expected+(size_t)incoming_count;
 if(!capacity)capacity=1;
 gfs_save_t *entries=(gfs_save_t*)calloc(capacity,sizeof(gfs_save_t));
 if(!entries){free(data);free(tmp);return -1;}
 int count=bytes?gfs_parse_index(data,entries,expected?expected:1):0;
 free(data);
 if(count<0||count!=expected){free(entries);free(tmp);return -1;}
 for(int i=0;i<incoming_count;i++){
  if(!valid(&incoming[i])){free(entries);free(tmp);return -1;}
  int dup=0;
  for(int j=0;j<count;j++){
   if(same(&entries[j],&incoming[i])){dup=1;break;}
   /* Never regress a verified download to an unverified PAGE row. */
   if(!strcmp(entries[j].page_url,incoming[i].page_url)&&
      !strcmp(incoming[i].format,"PAGE")&&
      entries[j].download_url[0]){dup=1;break;}
   /* Replace an unverified page record only when an actual file URL
    * has been extracted. Keep multiple distinct downloads per page. */
   if(!strcmp(entries[j].page_url,incoming[i].page_url)&&
      !strcmp(entries[j].format,"PAGE")&&
      !entries[j].download_url[0]&&incoming[i].download_url[0]){
    entries[j]=incoming[i];dup=1;break;
   }
  }
  if(dup)continue;
  if(count>=maximum_entries){free(entries);free(tmp);return -1;}
  entries[count++]=incoming[i];
 }
 f=fopen(tmp,"wb");
 if(!f){free(entries);free(tmp);return -1;}
 int ok=1;
 for(int i=0;i<count;i++){
  const gfs_save_t *e=&entries[i];
  if(fprintf(f,"%s\t%s\t%s\t%s\t%s\t%s\n",e->game,e->region,e->format,
             e->description,e->page_url,e->download_url)<0){ok=0;break;}
 }
 if(fflush(f)!=0)ok=0;
 if(fclose(f)!=0)ok=0;
 if(ok&&rename(tmp,path)!=0)ok=0;
 if(!ok)remove(tmp);
 free(entries);free(tmp);
 return ok?count:-1;
}
