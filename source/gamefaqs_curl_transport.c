#include "gamefaqs_curl_transport.h"
#include "gamefaqs_saves_index.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

/* GameFAQs-only diagnostics. No cookies, credentials or response bodies. */
#define GFS_LOG_PATH "/dev_hdd0/game/PSU000001/USRDIR/GAMEFAQS_HTTP.LOG"
#define GFS_LOG_OLD  "/dev_hdd0/game/PSU000001/USRDIR/GAMEFAQS_HTTP.OLD"
struct gfs_headers {char server[96],ray[96],mitigated[96],type[96];};
static void gfs_header_value(char *dst,size_t cap,const char *p,size_t n){
 while(n&&(*p==' '||*p=='\t')){p++;n--;}
 while(n&&(p[n-1]=='\r'||p[n-1]=='\n'||p[n-1]==' '))n--;
 if(n>=cap)n=cap-1;
 memcpy(dst,p,n);dst[n]=0;
}
static size_t gfs_header(char *ptr,size_t size,size_t count,void *ctx){
 struct gfs_headers *h=(struct gfs_headers*)ctx;
 if(size&&count>SIZE_MAX/size)return 0;
 size_t n=size*count;
 /* Clear on every HTTP response line, including redirect responses. */
 if(n>=5&&!memcmp(ptr,"HTTP/",5))memset(h,0,sizeof(*h));
 else {
  struct {const char *key;char *dst;size_t cap;} keys[]={
   {"server:",h->server,sizeof(h->server)},
   {"cf-ray:",h->ray,sizeof(h->ray)},
   {"cf-mitigated:",h->mitigated,sizeof(h->mitigated)},
   {"content-type:",h->type,sizeof(h->type)}
  };
  for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);i++){
   size_t k=strlen(keys[i].key);
   if(n>=k&&!strncasecmp(ptr,keys[i].key,k)){
    gfs_header_value(keys[i].dst,keys[i].cap,ptr+k,n-k);break;
   }
  }
 }
 return n;
}
static void gfs_log_http(const char *url,long status,int rc,int trusted,
 long version,size_t bytes,const struct gfs_headers *h){
 FILE *fp=fopen(GFS_LOG_PATH,"ab");
 if(!fp)return;
 if(fseek(fp,0,SEEK_END)==0){
  long size=ftell(fp);
  if(size>256L*1024L){
   fclose(fp);
   remove(GFS_LOG_OLD);
   rename(GFS_LOG_PATH,GFS_LOG_OLD);
   fp=fopen(GFS_LOG_PATH,"ab");if(!fp)return;
  }
 }
 fprintf(fp,"GameFAQs HTTP: url=%s status=%ld curl=%d trusted=%d http_version=%ld bytes=%lu\n",
  url,status,rc,trusted,version,(unsigned long)bytes);
 fprintf(fp,"server=%s cf-ray=%s cf-mitigated=%s content-type=%s\n---\n",
  h->server[0]?h->server:"(absent)",h->ray[0]?h->ray:"(absent)",
  h->mitigated[0]?h->mitigated:"(absent)",h->type[0]?h->type:"(absent)");
 fclose(fp);
}

int gfs_curl_fetch(const char *url,char **html,size_t *length,void *ctx){
 (void)ctx;
 if(!html||!length)return 0;
 *html=NULL;*length=0;
 unsigned gid=0,sid=0;
 int category=0,game_page=0;
 const char *base="https://gamefaqs.gamespot.com/ps2/category/999-all";
 if(url&&!strncmp(url,base,strlen(base))){
  const char *q=url+strlen(base);
  if(!*q)category=1;
  else if(!strncmp(q,"?page=",6)&&q[6]&&
          strspn(q+6,"0123456789")==strlen(q+6))
   category=strtoul(q+6,NULL,10)<=63;
 }
 if(url&&!category){
  const char *prefix="https://gamefaqs.gamespot.com/ps2/";
  if(!strncmp(url,prefix,strlen(prefix))){
   const char *p=url+strlen(prefix);
   if(*p>='0'&&*p<='9'){
    while(*p>='0'&&*p<='9')p++;
    if(*p=='-'){p++;while((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='-')p++;
     if(!*p)game_page=1;
    }
   }
  }
 }
 if(!url||(!category&&!game_page&&!gfs_parse_save_page_ids(url,&gid,&sid)))return 0;
 CURL *curl=curl_easy_init();if(!curl)return 0;
 struct gfs_buffer b={0};
 struct gfs_headers headers={0};
 curl_easy_setopt(curl,CURLOPT_HEADERFUNCTION,gfs_header);
 curl_easy_setopt(curl,CURLOPT_HEADERDATA,&headers);
 curl_easy_setopt(curl,CURLOPT_URL,url);
 curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,gfs_write);
 curl_easy_setopt(curl,CURLOPT_WRITEDATA,&b);
 curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,15L);
 curl_easy_setopt(curl,CURLOPT_TIMEOUT,45L);
 /* Follow a bounded number of redirects; validate the final host below. */
 curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
 curl_easy_setopt(curl,CURLOPT_MAXREDIRS,5L);
 /* Never permit a redirect from HTTPS to HTTP or a non-web protocol. */
 curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS,(long)CURLPROTO_HTTPS);
 curl_easy_setopt(curl,CURLOPT_FAILONERROR,0L);
 curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
 curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
 curl_easy_setopt(curl,CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2);
 curl_easy_setopt(curl,CURLOPT_CAINFO,"/dev_hdd0/game/PSU000001/USRDIR/cacert.pem");
 /* Diagnostic A/B test: present a desktop Chrome User-Agent for GameFAQs
  * only. This changes no TLS fingerprint or browser challenge handling. */
 curl_easy_setopt(curl,CURLOPT_USERAGENT,
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
  "(KHTML, like Gecko) Chrome/156.0.0.0 Safari/537.36");
 CURLcode rc=curl_easy_perform(curl);
 long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
 char *effective=NULL;curl_easy_getinfo(curl,CURLINFO_EFFECTIVE_URL,&effective);
 long http_version=0;
 curl_easy_getinfo(curl,CURLINFO_HTTP_VERSION,&http_version);
 unsigned final_game=0,final_save=0;
 int trusted=0;
 if(category||game_page)trusted=effective&&!strcmp(effective,url);
 else trusted=effective&&gfs_parse_save_page_ids(effective,&final_game,&final_save)&&final_game==gid&&final_save==sid;
 gfs_log_http(url,status,(int)rc,trusted,http_version,b.used,&headers);
 /* CachedView's actual View Cached Page button opens
  * https://web.archive.org/web/2/<original-url>.  Use the same
  * public Wayback lookup only when live GameFAQs is denied.
  * Never treat archived save links as verified downloads. */
 if(rc==CURLE_OK&&!b.failed&&status==403&&
    !strcasecmp(headers.mitigated,"challenge")){
  size_t n=strlen(url);
  const char *archive_prefix="https://web.archive.org/web/2/";
  char *archive_url=(char*)malloc(strlen(archive_prefix)+n+1);
  if(archive_url){
   sprintf(archive_url,"%s%s",archive_prefix,url);
   free(b.data);b.data=NULL;b.used=0;b.failed=0;
   memset(&headers,0,sizeof(headers));
   curl_easy_setopt(curl,CURLOPT_URL,archive_url);
   CURLcode archive_rc=curl_easy_perform(curl);
   long archive_status=0;
   curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&archive_status);
   char *archive_effective=NULL;
   curl_easy_getinfo(curl,CURLINFO_EFFECTIVE_URL,&archive_effective);
   long archive_version=0;
   curl_easy_getinfo(curl,CURLINFO_HTTP_VERSION,&archive_version);
   /* Only accept HTTPS Wayback replay pages for this exact original
    * GameFAQs URL; reject external redirects and unrelated content. */
   const char *wayback="https://web.archive.org/web/";
   int archive_trusted=0;
   if(archive_effective&&!strncmp(archive_effective,wayback,strlen(wayback))){
    const char *replay=strstr(archive_effective+strlen(wayback),"/https://gamefaqs.gamespot.com/");
    if(replay&&!strcmp(replay+1,url))archive_trusted=1;
   }
   gfs_log_http(archive_url,archive_status,(int)archive_rc,
                archive_trusted,archive_version,b.used,&headers);
   if(archive_rc==CURLE_OK&&!b.failed&&archive_status==200&&archive_trusted){
    fprintf(stderr,"GameFAQs: archived snapshot loaded via CachedView Wayback lookup: %s\\n",archive_effective);
    curl_easy_cleanup(curl);
    free(archive_url);
    if(!b.data){b.data=(char*)malloc(1);if(!b.data)return 0;b.data[0]=0;}
    *html=b.data;*length=b.used;return 200;
   }
   fprintf(stderr,"GameFAQs Wayback lookup failed: curl=%d http=%ld trusted=%d\\n",
           (int)archive_rc,archive_status,archive_trusted);
   free(archive_url);
   rc=archive_rc;status=archive_status;trusted=archive_trusted;
  }
 }

 if(rc!=CURLE_OK||b.failed||status!=200||!trusted){
  fprintf(stderr,"GameFAQs fetch [Chrome-UA trial]: curl=%d http=%ld overflow=%d trusted=%d url=%s\n",(int)rc,status,b.failed,trusted,url);
  curl_easy_cleanup(curl);
  free(b.data);
  /* Return a distinct negative error for TLS/network/redirect failures. */
  return rc!=CURLE_OK?-(int)rc:(b.failed?-1001:(status!=200?(int)status:-1002));
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
