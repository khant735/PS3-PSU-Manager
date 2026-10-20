#include "gamefaqs_saves_index.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
static int host_match(const char *url,const char *host){
 if(strncmp(url,"https://",8))return 0;
 const char *p=url+8,*e=strpbrk(p,"/:?#");
 size_t n=e?(size_t)(e-p):strlen(p);
 if(n!=strlen(host)||strncmp(p,host,n))return 0;
 return !e||*e=='/'||*e=='?';
}
static int safe_url(const char *url){
 if(!url||strlen(url)>500||strpbrk(url,"\r\n\t\\@ "))return 0;
 return host_match(url,"gamefaqs.gamespot.com")||host_match(url,"www.gamefaqs.com");
}
int gfs_download_allowed(const char *url){
 if(!safe_url(url))return 0;
 const char *end=strchr(url,'?');
 size_t n=end?(size_t)(end-url):strlen(url);
 const char *exts[]={".psu",".max",".cbs",".xps",".sps",".xpo",".spo",".zip",".psv"};
 for(unsigned i=0;i<sizeof(exts)/sizeof(exts[0]);i++){size_t z=strlen(exts[i]);if(n>=z&&!strncasecmp(url+n-z,exts[i],z))return 1;}
 return 0;
}
int gfs_bucket(const char *s){
 while(*s&&isspace((unsigned char)*s))s++;
 unsigned char c=(unsigned char)*s;
 if(c>='a'&&c<='z')c-=32;
 return c>='A'&&c<='Z'?c-'A'+1:0;
}
int gfs_parse_index(const char *data,gfs_save_t *out,int cap){
 if(!data||!out||cap<=0)return -1;
 int count=0;
 const char *line=data;
 while(*line){
  const char *end=strchr(line,'\n');if(!end)end=line+strlen(line);
  size_t n=(size_t)(end-line);
  if(n>0&&n<1800&&*line!='#'){
   char buf[1800];memcpy(buf,line,n);buf[n]=0;
   if(n&&buf[n-1]=='\r')buf[n-1]=0;
   char *parts[6],*p=buf;int fields=0;
   parts[fields++]=p;
   while(*p){if(*p=='\t'){*p=0;if(fields>=6){fields=7;break;}parts[fields++]=p+1;}p++;}
   if(fields==6&&parts[0][0]&&parts[1][0]&&parts[2][0]&&safe_url(parts[4])&&(!parts[5][0]||gfs_download_allowed(parts[5]))&&(!strcmp(parts[2],"PAGE")?!parts[5][0]:parts[5][0]!=0)){
    if(strlen(parts[0])<128&&strlen(parts[1])<48&&strlen(parts[2])<24&&strlen(parts[3])<192&&strlen(parts[4])<384&&strlen(parts[5])<512){
     if(count>=cap)break;
     gfs_save_t *e=&out[count++];memset(e,0,sizeof(*e));
     snprintf(e->game,sizeof(e->game),"%s",parts[0]);
     snprintf(e->region,sizeof(e->region),"%s",parts[1]);
     snprintf(e->format,sizeof(e->format),"%s",parts[2]);
     snprintf(e->description,sizeof(e->description),"%s",parts[3]);
     snprintf(e->page_url,sizeof(e->page_url),"%s",parts[4]);
     snprintf(e->download_url,sizeof(e->download_url),"%s",parts[5]);
    }
   }
  }
  line=*end?end+1:end;
 }
 return count;
}

/* Parse stable GameFAQs /ps2/<id>-<slug>/saves[/<save-id>] page identifiers.
   The save-detail page is NOT a direct download URL. */
int gfs_parse_save_page_ids(const char *url, unsigned *game_id, unsigned *save_id){
 if(!safe_url(url)||!game_id||!save_id)return 0;
 const char *p=strchr(url+8,'/');
 if(!p||strncmp(p,"/ps2/",5))return 0;
 p+=5;
 unsigned game=0,save=0;
 if(!isdigit((unsigned char)*p))return 0;
 while(isdigit((unsigned char)*p)){
  unsigned digit=(unsigned)(*p++-'0');
  if(game>100000000U)return 0;
  game=game*10+digit;
 }
 if(*p!='-')return 0;
 p++;
 const char *slug=p;
 while(isalnum((unsigned char)*p)||*p=='-')p++;
 if(p==slug||strncmp(p,"/saves",6))return 0;
 p+=6;
 if(*p&&*p!='/'&&*p!='?'&&*p!='#')return 0;
 if(*p=='/'){
  p++;
  if(!isdigit((unsigned char)*p))return 0;
  while(isdigit((unsigned char)*p)){
   unsigned digit=(unsigned)(*p++-'0');
   if(save>100000000U)return 0;
   save=save*10+digit;
  }
 }
 if(*p&&*p!='?'&&*p!='#')return 0;
 *game_id=game;*save_id=save;
 return game!=0;
}
