#include "gamefaqs_crawl.h"
#include "gamefaqs_discovery_pipeline.h"
#include "gamefaqs_html_native.h"
#include "gamefaqs_saves_index.h"
#include "gamefaqs_index_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GFS_HTML_LIMIT (1024U*1024U)
#define GFS_DETAIL_LIMIT 256
static int read_page(const char *url,gfs_fetch_fn fetch,gfs_release_fn release,
 void *ctx,char **html,size_t *length){
 *html=NULL;*length=0;
 int status=fetch(url,html,length,ctx);
 if(status!=200||!*html){if(*html)release(*html,ctx);*html=NULL;return -1;}
 if(*length>GFS_HTML_LIMIT||memchr(*html,0,*length)!=NULL||
    (*html)[*length]!=0){release(*html,ctx);*html=NULL;return -1;}
 return 0;
}
/* Diagnostics use the same strict URL parser as the crawler.  Never
 * classify navigation links merely because they contain 'save' or 'game'. */
static void diagnose_listing_links(const char *html,unsigned listing_gid){
 char (*urls)[384]=calloc(GFS_DETAIL_LIMIT,sizeof(*urls));
 if(!urls)return;
 int found=gfs_discover_save_links(html,urls,GFS_DETAIL_LIMIT);
 int accepted=0,other_game=0;
 if(found>0)for(int i=0;i<found;i++){
  unsigned gid=0,sid=0;
  if(!gfs_parse_save_page_ids(urls[i],&gid,&sid)||!sid)continue;
  if(gid!=listing_gid){other_game++;continue;}
  accepted++;
  if(accepted<=12)fprintf(stderr,"GameFAQs valid PS2 save[%d]: %.220s\n",accepted-1,urls[i]);
 }
 int hrefs=0;const char *p=html;
 while((p=strstr(p,"href"))!=NULL&&hrefs<4096){
  const char *q=p+4;
  while(*q==' '||*q=='\\t')q++;
  if(*q=='=')hrefs++;
  p+=4;
 }
 fprintf(stderr,"GameFAQs link diagnostic: hrefs=%d valid_ps2_details=%d other_game=%d non_save_or_duplicate=%d\n",
         hrefs,accepted,other_game,hrefs-accepted-other_game);
 free(urls);
}

/* Keep every detail request inside the timestamped Wayback snapshot. */
static int archived_detail_url(const char *listing,const char *detail,char *out,size_t cap){
 const char *base="https://web.archive.org/web/";
 if(strncmp(listing,base,strlen(base))){snprintf(out,cap,"%s",detail);return 1;}
 const char *p=listing+strlen(base);char stamp[15];
 for(int i=0;i<14;i++){if(p[i]<'0'||p[i]>'9')return 0;stamp[i]=p[i];}
 stamp[14]=0;
 const char *inner=detail;
 if(!strncmp(detail,base,strlen(base))){
  const char *q=detail+strlen(base);int i=0;
  while(i<14&&q[i]>='0'&&q[i]<='9')i++;
  if(i!=14)return 0;q+=14;
  if(!strncmp(q,"id_/",4)||!strncmp(q,"if_/",4))q+=4;
  else if(*q=='/')q++;
  else return 0;
  inner=q;
 }
 int n=snprintf(out,cap,"%s%s/%s",base,stamp,inner);
 return n>0&&(size_t)n<cap;
}

/* Query actual Wayback captures; never guess timestamps or issue a request
 * for every second between 1995 and 2026. CDX returns newest first. */
static int historical_detail(const char *original,gfs_fetch_fn fetch,
 gfs_release_fn release,void *ctx,char **html,size_t *length,
 char *resolved,size_t cap){
 const char *endpoint="https://web.archive.org/cdx/search/cdx?url=";
 char encoded[1024];size_t k=0;
 for(const unsigned char *p=(const unsigned char*)original;*p;p++){
  unsigned char c=*p;
  if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
     (c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~'){
   if(k+1>=sizeof(encoded))return -1;encoded[k++]=(char)c;
  }else{
   if(k+3>=sizeof(encoded))return -1;
   snprintf(encoded+k,4,"%%%02X",(unsigned)c);k+=3;
  }
 }
 encoded[k]=0;
 /* CDX paging preserves chronological order without storing thousands
  * of captures in PS3 memory. Each page is processed before the next. */
 for(int page=0;page<128;page++){
  char query[1536];
  int n=snprintf(query,sizeof(query),
   "%s%s&from=1995&to=2026&output=txt&fl=timestamp&filter=statuscode%%3A200&sort=reverse&limit=100&offset=%d",
   endpoint,encoded,page*100);
  if(n<=0||(size_t)n>=sizeof(query))return -1;
  char *body=NULL;size_t bytes=0;
  if(read_page(query,fetch,release,ctx,&body,&bytes))return -1;
  int count=0;char *p=body;
  while(*p){
   char stamp[15];int j=0;
   while(p[j]>='0'&&p[j]<='9'&&j<14){stamp[j]=p[j];j++;}
   if(j==14&&(p[j]=='\n'||p[j]=='\r'||p[j]==0)){
    stamp[14]=0;count++;
    n=snprintf(resolved,cap,"https://web.archive.org/web/%s/%s",stamp,original);
    if(n>0&&(size_t)n<cap){
     fprintf(stderr,"GameFAQs CDX candidate: %s\n",resolved);
     if(!read_page(resolved,fetch,release,ctx,html,length)){
      fprintf(stderr,"GameFAQs CDX detail fetched: %s\n",resolved);
      release(body,ctx);return 0;
     }
    }
   }
   while(*p&&*p!='\n')p++;
   if(*p=='\n')p++;
  }
  release(body,ctx);
  if(count<100)break;
 }
 return -1;
}

int gfs_crawl_listing_html(const char *listing_url,const char *html,const char *index_path,
 const char *game,const char *region,int maximum_entries,
 gfs_fetch_fn fetch,gfs_release_fn release,void *ctx){
 unsigned gid=0,sid=0;
 if(!listing_url||!html||!index_path||!game||!region||!fetch||!release||
    !gfs_parse_save_page_ids(listing_url,&gid,&sid)||sid||
    maximum_entries<1||maximum_entries>GFS_MAX_ENTRIES)return -1;
 char (*urls)[384]=calloc(GFS_DETAIL_LIMIT,sizeof(*urls));
 if(!urls)return -1;
 int found=gfs_discover_save_links(html,urls,GFS_DETAIL_LIMIT);
 if(found<0){free(urls);return -1;}
 fprintf(stderr,"GameFAQs listing links: discovered=%d url=%s\n",found,listing_url);
 int matched=0, fetched=0, verified=0, failed=0;
 size_t length=0;
 for(int i=0;i<found;i++){
  unsigned detail_game=0,detail_id=0;
  if(!gfs_parse_save_page_ids(urls[i],&detail_game,&detail_id)||
     detail_game!=gid||!detail_id)continue;
  matched++;
  char *detail_html=NULL;
  char fetch_url[512];
  if(!archived_detail_url(listing_url,urls[i],fetch_url,sizeof(fetch_url))){failed++;continue;}
  if(read_page(fetch_url,fetch,release,ctx,&detail_html,&length)){
   /* A listing capture does not imply its detail was archived at the
    * same timestamp. Retry the original URL through the caller's
    * CachedView/Wayback resolver, which selects its own capture. */
   const char *original=urls[i];
   const char *wayback="https://web.archive.org/web/";
   if(!strncmp(original,wayback,strlen(wayback))){
    const char *q=original+strlen(wayback);
    while(*q>='0'&&*q<='9')q++;
    if(!strncmp(q,"id_/",4)||!strncmp(q,"if_/",4))q+=4;
    else if(*q=='/')q++;
    if(!strncmp(q,"https://gamefaqs.gamespot.com/",30)||
       !strncmp(q,"https://www.gamefaqs.com/",25))original=q;
   }
   if(historical_detail(original,fetch,release,ctx,&detail_html,&length,
                        fetch_url,sizeof(fetch_url)) &&
      (!strcmp(original,fetch_url)||
       read_page(original,fetch,release,ctx,&detail_html,&length))){
    fprintf(stderr,"GameFAQs detail unavailable after historical CDX search: %s\n",urls[i]);
    failed++;
    continue;
   }
   fprintf(stderr,"GameFAQs detail recovered: %s\n",fetch_url);
  }
  fetched++;
  /* Never add PAGE placeholders: only validated direct download URLs
   * returned by the detail parser are eligible for the catalogue. */
  gfs_save_t *records=(gfs_save_t*)calloc(64,sizeof(*records));
  if(!records){release(detail_html,ctx);failed++;continue;}
  int n=gfs_extract_detail_html(detail_html,fetch_url,game,region,records,64);
  release(detail_html,ctx);
  if(n<=0){free(records);if(n<0)failed++;continue;}
  int merged=gfs_merge_index_file(index_path,records,n,maximum_entries);
  free(records);
  if(merged<0){
   fprintf(stderr,"GameFAQs detail merge failed: %s\n",urls[i]);
   failed++;
  }else verified+=n;
 }
 fprintf(stderr,"GameFAQs detail summary: matched=%d fetched=%d verified_urls=%d failed=%d listing=%s\n",
         matched,fetched,verified,failed,listing_url);
 if(!matched){fprintf(stderr,"GameFAQs no recognized individual save links: %s\n",listing_url);diagnose_listing_links(html,gid);}

 free(urls);
 return verified;
}

int gfs_crawl_listing(const char *listing_url,const char *index_path,
 const char *game,const char *region,int maximum_entries,
 gfs_fetch_fn fetch,gfs_release_fn release,void *ctx){
 if(!listing_url||!fetch||!release)return -1;
 char *html=NULL;size_t length=0;
 if(read_page(listing_url,fetch,release,ctx,&html,&length))return -1;
 int result=gfs_crawl_listing_html(listing_url,html,index_path,game,region,
                                  maximum_entries,fetch,release,ctx);
 release(html,ctx);
 return result;
}
