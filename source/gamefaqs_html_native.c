#include "gamefaqs_html_native.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int prefix(const char *s,const char *p){return strncmp(s,p,strlen(p))==0;}
static void copy(char *dst,size_t cap,const char *src){snprintf(dst,cap,"%s",src);}
static const char *format_for(const char *url){
 const char *q=strpbrk(url,"?#");size_t len=q?(size_t)(q-url):strlen(url);
 static const struct {const char *ext,*fmt;} exts[]={
 {".psu","PSU"},{".max","MAX"},{".cbs","CBS"},{".xps","XPS"},
 {".sps","SPS"},{".xpo","XPO"},{".spo","SPO"},{".psv","PSV"},{".zip","ZIP"}};
 for(size_t i=0;i<sizeof(exts)/sizeof(exts[0]);i++){
  size_t n=strlen(exts[i].ext);if(len<n)continue;
  size_t j=0;for(;j<n;j++)if(tolower((unsigned char)url[len-n+j])!=exts[i].ext[j])break;
  if(j==n)return exts[i].fmt;
 }return NULL;
}
int gfs_extract_detail_html(const char *html,const char *page_url,
 const char *game,const char *region,gfs_save_t *out,int capacity){
 unsigned game_id=0,save_id=0;
 if(!html||!page_url||!game||!region||!out||capacity<=0||
    !gfs_parse_save_page_ids(page_url,&game_id,&save_id)||!save_id)return -1;
 int count=0;const char *p=html;
 while((p=strstr(p,"href"))!=NULL){
  const char *a=p+4;while(*a==' '||*a=='\t')a++;
  if(*a!='='){p+=4;continue;}a++;
  while(*a==' '||*a=='\t')a++;
  char quote=*a;if(quote!='\''&&quote!='\"'){p+=4;continue;}a++;
  const char *end=strchr(a,quote);if(!end)break;
  size_t n=(size_t)(end-a);p=end+1;
  if(n==0||n>=512)continue;
  char url[512];memcpy(url,a,n);url[n]=0;
  /* Protocol-relative links must retain HTTPS, not gain a duplicate host. */
  if(prefix(url,"//")){
   if(strlen(url)+strlen("https:")>=sizeof(url))continue;
   char tmp[512];snprintf(tmp,sizeof(tmp),"https:%s",url);
   copy(url,sizeof(url),tmp);
  }else if(prefix(url,"/")){
   if(strlen(url)+strlen("https://gamefaqs.gamespot.com")>=sizeof(url))continue;
   char tmp[512];const char *host="https://gamefaqs.gamespot.com";size_t hn=strlen(host);memcpy(tmp,host,hn);memcpy(tmp+hn,url,strlen(url)+1);copy(url,sizeof(url),tmp);
  }
  /* Entity-encoded href values require HTML decoding before use. */
  if(strchr(url,38)||strpbrk(url,"\\\r\n\t "))continue;
  if(!gfs_download_allowed(url)||!format_for(url))continue;
  int duplicate=0;for(int i=0;i<count;i++)if(!strcmp(out[i].download_url,url))duplicate=1;
  if(duplicate)continue;
  if(count>=capacity)break;
  gfs_save_t *entry=&out[count++];memset(entry,0,sizeof(*entry));
  copy(entry->game,sizeof(entry->game),game);
  copy(entry->region,sizeof(entry->region),region);
  copy(entry->format,sizeof(entry->format),format_for(url));
  snprintf(entry->description,sizeof(entry->description),"GameFAQs save %u",save_id);
  copy(entry->page_url,sizeof(entry->page_url),page_url);
  copy(entry->download_url,sizeof(entry->download_url),url);
 }
 return count;
}


/* Discover explicit PS2 save-list and save-detail links from catalogue HTML.
 * Only gamefaqs.gamespot.com links are accepted; no guessed endpoints.
 * Output is capacity records of 384 bytes each, NUL terminated. */
int gfs_discover_save_links(const char *html, char (*urls)[384], int capacity){
 if(!html||!urls||capacity<=0)return -1;
 int count=0;const char *p=html;
 while((p=strstr(p,"href"))!=NULL){
  const char *a=p+4;while(*a==' '||*a=='\t')a++;
  if(*a!='='){p+=4;continue;}a++;
  while(*a==' '||*a=='\t')a++;
  char quote=*a;if(quote!='\''&&quote!='"'){p+=4;continue;}a++;
  const char *end=strchr(a,quote);if(!end)break;
  size_t n=(size_t)(end-a);p=end+1;
  if(!n||n>=384)continue;
  char url[384];memcpy(url,a,n);url[n]=0;
  /* Accept canonical HTTPS protocol-relative GameFAQs page links. */
  if(prefix(url,"//gamefaqs.gamespot.com/ps2/")||
     prefix(url,"//www.gamefaqs.com/ps2/")){
   char full[384];
   if(strlen(url)+strlen("https:")>=sizeof(full))continue;
   snprintf(full,sizeof(full),"https:%s",url);
   copy(url,sizeof(url),full);
  }
  if(prefix(url,"/ps2/")){
   char full[384];const char *host="https://gamefaqs.gamespot.com";
   if(strlen(host)+n>=sizeof(full))continue;
   snprintf(full,sizeof(full),"%s%s",host,url);
   copy(url,sizeof(url),full);
  }
  /* Do not misindex HTML entity-encoded links as raw URLs. */
  if(strchr(url,38)||strpbrk(url,"\\\r\n\t "))continue;
  unsigned game_id=0,save_id=0;
  if(!gfs_parse_save_page_ids(url,&game_id,&save_id)||!save_id)continue;
  /* A /saves listing link is not an individual save entry. */
  /* Drop fragment/query parameters for a stable deduplication key. */
  char *tail=strpbrk(url,"?#");if(tail)*tail=0;
  int duplicate=0;
  for(int i=0;i<count;i++)if(!strcmp(urls[i],url)){duplicate=1;break;}
  if(duplicate)continue;
  if(count>=capacity)break;
  copy(urls[count++],384,url);
 }
 return count;
}
