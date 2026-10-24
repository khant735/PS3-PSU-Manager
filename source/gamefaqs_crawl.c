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
/* Bounded, public-link diagnostics for archive HTML with no recognized save IDs. */
static void diagnose_listing_links(const char *html){
 const char *p=html;int hrefs=0,saves=0,shown=0;
 while((p=strstr(p,"href"))!=NULL && hrefs<4096){
  const char *a=p+4;while(*a==' '||*a=='\t')a++;
  if(*a!='='){p+=4;continue;}a++;
  while(*a==' '||*a=='\t')a++;
  char q=*a;if(q!='\''&&q!='"'){p+=4;continue;}a++;
  const char *end=strchr(a,q);if(!end)break;
  size_t n=(size_t)(end-a);p=end+1;hrefs++;
  if(!n||n>=384)continue;
  char url[384];memcpy(url,a,n);url[n]=0;
  if(strstr(url,"save")||strstr(url,"download")){
   saves++;
   if(shown<12){
    /* Avoid query strings, tokens and excessive output. */
    char *cut=strpbrk(url,"?#");if(cut)*cut=0;
    fprintf(stderr,"GameFAQs candidate href[%d]: %.220s\n",shown,url);
    shown++;
   }
  }
 }
 fprintf(stderr,"GameFAQs href diagnostic: total=%d save_related=%d shown=%d\n",hrefs,saves,shown);
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
   fprintf(stderr,"GameFAQs detail unavailable: %s\n",urls[i]);
   failed++;
   continue;
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
 if(!matched){fprintf(stderr,"GameFAQs no recognized individual save links: %s\n",listing_url);diagnose_listing_links(html);}

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
