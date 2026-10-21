#include "gamefaqs_curl_transport.h"
#include "gamefaqs_saves_index.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#define GFS_MAX_HTML (1024U*1024U)
struct gfs_buffer {char *data;size_t used;int failed;};
static size_t gfs_write(char *p,size_t size,size_t count,void *user){
 struct gfs_buffer *b=(struct gfs_buffer*)user;
 if(size && count>SIZE_MAX/size)return 0;
 size_t n=size*count;
 if(n>GFS_MAX_HTML-b->used){b->failed=1;return 0;}
 char *next=(char*)realloc(b->data,b->used+n+1);
 if(!next){b->failed=1;return 0;}
 b->data=next;memcpy(b->data+b->used,p,n);
 b->used+=n;b->data[b->used]=0;return n;
}
int gfs_curl_fetch(const char *url,char **html,size_t *length,void *ctx){
 (void)ctx;
 if(!html||!length)return 0;
 *html=NULL;*length=0;
 unsigned gid=0,sid=0;
 if(!url||!gfs_parse_save_page_ids(url,&gid,&sid))return 0;
 CURL *curl=curl_easy_init();if(!curl)return 0;
 struct gfs_buffer b={0};
 curl_easy_setopt(curl,CURLOPT_URL,url);
 curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,gfs_write);
 curl_easy_setopt(curl,CURLOPT_WRITEDATA,&b);
 curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,15L);
 curl_easy_setopt(curl,CURLOPT_TIMEOUT,45L);
 /* Follow a bounded number of redirects; validate the final host below. */
 curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
 curl_easy_setopt(curl,CURLOPT_MAXREDIRS,5L);
 curl_easy_setopt(curl,CURLOPT_FAILONERROR,0L);
 curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
 curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
 curl_easy_setopt(curl,CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2);
 curl_easy_setopt(curl,CURLOPT_CAINFO,"/dev_hdd0/game/PSU000001/USRDIR/cacert.pem");
 curl_easy_setopt(curl,CURLOPT_USERAGENT,"PS3-PSU-Manager/1.63T Save-Index");
 CURLcode rc=curl_easy_perform(curl);
 long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
 char *effective=NULL;curl_easy_getinfo(curl,CURLINFO_EFFECTIVE_URL,&effective);
 unsigned final_game=0,final_save=0;
 int trusted=effective&&gfs_parse_save_page_ids(effective,&final_game,&final_save)&&final_game==gid&&final_save==sid;
 if(rc!=CURLE_OK||b.failed||status!=200||!trusted){
  fprintf(stderr,"GameFAQs fetch: curl=%d http=%ld overflow=%d trusted=%d url=%s\n",(int)rc,status,b.failed,trusted,url);
  curl_easy_cleanup(curl);
  free(b.data);return (int)(status?status:0);
 }
 curl_easy_cleanup(curl);
 if(!b.data){b.data=(char*)malloc(1);if(!b.data)return 0;b.data[0]=0;}
 *html=b.data;*length=b.used;return 200;
}
void gfs_curl_release(char *html,void *ctx){(void)ctx;free(html);}

int gfs_crawl_listing_https(const char *listing_url,const char *index_path,
 const char *game,const char *region,int maximum_entries){
 return gfs_crawl_listing(listing_url,index_path,game,region,maximum_entries,
                          gfs_curl_fetch,gfs_curl_release,NULL);
}
