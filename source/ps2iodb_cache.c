#include "ps2iodb_cache.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

/* Verified against upstream iconsys.json, 19 October 2026.
   These are regional candidates, NOT the original European icons. */
static const ps2iodb_match known[] = {
 {"BASLUS-20187GAMEDATA","NBA Street","nbastreet","icon.ico","US",1},
 {"BASLUS-20130ARKSFEX3","Street Fighter EX3","streetfighterex3","slime1.ico","US",1}
};
int ps2iodb_lookup(const char *dir,ps2iodb_match *out){
 if(!dir||!out)return 0;
 for(unsigned i=0;i<sizeof(known)/sizeof(known[0]);++i)
  if(!strcmp(dir,known[i].directory)){*out=known[i];return 1;}
 if(!strcmp(dir,"BESLES-50219GAMEDATA")){*out=known[0];out->exact=0;return 2;}
 if(!strcmp(dir,"BESLES-50072ARKSFEX3")){*out=known[1];out->exact=0;return 2;}
 return 0;
}
static int safe_component(const char *s){if(!s||!*s||strlen(s)>64)return 0;for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='.'||*s=='_'||*s=='-'))return 0;return 1;}
static int mkdir_if_needed(const char *p){if(mkdir(p,0775)==0||errno==EEXIST)return 0;return -1;}
static size_t writer(void *ptr,size_t size,size_t count,void *f){return fwrite(ptr,size,count,(FILE*)f);}
static int fetch_one(const char *slug,const char *name,const char *dest,const char *ca){
 char url[512],tmp[1024];struct stat st;
 if(stat(dest,&st)==0&&st.st_size>0&&st.st_size<=8*1024*1024)return 0;
 if(snprintf(url,sizeof(url),"https://raw.githubusercontent.com/Issung/PS2IODB/main/website/public/icons/%s/%s",slug,name)>=(int)sizeof(url))return -1;
 if(snprintf(tmp,sizeof(tmp),"%s.part",dest)>=(int)sizeof(tmp))return -1;
 FILE *f=fopen(tmp,"wb");if(!f)return -1;
 CURL *c=curl_easy_init();if(!c){fclose(f);unlink(tmp);return -1;}
 curl_easy_setopt(c,CURLOPT_URL,url);
 curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,writer);
 curl_easy_setopt(c,CURLOPT_WRITEDATA,f);
 curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,0L);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
 curl_easy_setopt(c,CURLOPT_CAINFO,ca);
 curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,10L);
 curl_easy_setopt(c,CURLOPT_TIMEOUT,90L);
 curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
 curl_easy_setopt(c,CURLOPT_MAXFILESIZE,8L*1024L*1024L);
 curl_easy_setopt(c,CURLOPT_USERAGENT,"PS3-PSU-Manager/1.63 PS2IODB-icon-cache");
 CURLcode rc=curl_easy_perform(c);long status=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);
 curl_easy_cleanup(c);fclose(f);
 if(rc!=CURLE_OK||status!=200){unlink(tmp);return -1;}
 if(stat(tmp,&st)!=0||st.st_size<=0||st.st_size>8*1024*1024){unlink(tmp);return -1;}
 if(rename(tmp,dest)!=0){unlink(tmp);return -1;}
 return 0;
}
int ps2iodb_cache_fetch(const ps2iodb_match *m,const char *root,const char *ca){
 char folder[1024],dest[1024],file[128];
 if(!m||!root||!ca||!safe_component(m->slug)||!safe_component(m->model))return -1;
 if(mkdir_if_needed(root)!=0)return -1;
 if(snprintf(folder,sizeof(folder),"%s/%s",root,m->slug)>=(int)sizeof(folder))return -1;
 if(mkdir_if_needed(folder)!=0)return -1;
 const char *suffix[]={".obj",".mtl",".png",".anim"};
 if(snprintf(dest,sizeof(dest),"%s/iconsys.json",folder)>=(int)sizeof(dest))return -1;
 if(fetch_one(m->slug,"iconsys.json",dest,ca)!=0)return -1;
 for(int i=0;i<4;i++){
  if(snprintf(file,sizeof(file),"%s%s",m->model,suffix[i])>=(int)sizeof(file))return -1;
  if(snprintf(dest,sizeof(dest),"%s/%s",folder,file)>=(int)sizeof(dest))return -1;
  if(fetch_one(m->slug,file,dest,ca)!=0){if(i==3)continue;return -1;} /* animation optional */
 }
 return 0;
}
