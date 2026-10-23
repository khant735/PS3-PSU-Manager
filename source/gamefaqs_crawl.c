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
 int result=0;
 int discovered=0;
 for(int i=0;i<found;i++){
  unsigned dg=0,ds=0;
  if(!gfs_parse_save_page_ids(urls[i],&dg,&ds)||dg!=gid||!ds)continue;
  gfs_save_t page={0};
  snprintf(page.game,sizeof(page.game),"%s",game);
  snprintf(page.region,sizeof(page.region),"%s",region);
  snprintf(page.format,sizeof(page.format),"PAGE");
  snprintf(page.description,sizeof(page.description),"GameFAQs save %u",ds);
  snprintf(page.page_url,sizeof(page.page_url),"%s",urls[i]);
  result=gfs_merge_index_file(index_path,&page,1,maximum_entries);
  if(result<0){free(urls);return -1;}
  discovered++;
 }
 /* An HTTP 200 response is not proof that the saves listing was parsed. */
 if(!discovered){
  /* A valid HTTP response may contain no individual save links. This is
   * not an index write failure; retain all previously indexed records. */
  fprintf(stderr,"GameFAQs listing has no recognized save-detail links: %s (href matches=%d)\n",listing_url,found);
  free(urls);
  return 0;
 }
 /* Keep the discovered PAGE entries even when some individual detail pages
  * cannot be fetched. Their download URLs remain empty until verified. */
 int processed=1;
 size_t length=0;
 for(int i=0;i<found;i++){
  unsigned detail_game=0,detail_id=0;
  if(!gfs_parse_save_page_ids(urls[i],&detail_game,&detail_id)||
     detail_game!=gid||!detail_id)continue;
  char *detail_html=NULL;
  if(read_page(urls[i],fetch,release,ctx,&detail_html,&length)){
   fprintf(stderr,"GameFAQs detail unavailable: %s\n",urls[i]);
   continue;
  }
  result=gfs_ingest_html(index_path,detail_html,urls[i],game,region,maximum_entries);
  release(detail_html,ctx);
  if(result<0)fprintf(stderr,"GameFAQs detail index merge failed: %s\n",urls[i]);
  else processed++;
 }
 free(urls);
 return processed;
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
