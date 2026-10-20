#include "gamefaqs_html_native.h"
#include <stdio.h>
#include <string.h>

/* Convert discovered save-detail pages into the existing six-column save
 * index. No page URL is ever misrepresented as a verified file download.
 * Caller must fetch and validate a direct file URL separately. */
int gfs_index_save_pages(const char *html,const char *game,const char *region,
                         gfs_save_t *out,int capacity){
 if(!html||!game||!region||!out||capacity<=0)return -1;
 if(!*game||!*region||strlen(game)>=sizeof(out[0].game)||
    strlen(region)>=sizeof(out[0].region)||
    strpbrk(game,"\t\r\n")||strpbrk(region,"\t\r\n"))return -1;
 char urls[64][384];
 int limit=capacity<64?capacity:64;
 int n=gfs_discover_save_links(html,urls,limit);
 if(n<0)return -1;
 int count=0;
 for(int i=0;i<n;i++){
  unsigned game_id=0,save_id=0;
  if(!gfs_parse_save_page_ids(urls[i],&game_id,&save_id)||!save_id)continue;
  gfs_save_t *e=&out[count++];
  memset(e,0,sizeof(*e));
  snprintf(e->game,sizeof(e->game),"%s",game);
  snprintf(e->region,sizeof(e->region),"%s",region);
  snprintf(e->format,sizeof(e->format),"PAGE");
  snprintf(e->description,sizeof(e->description),"GameFAQs save %u (download unverified)",save_id);
  snprintf(e->page_url,sizeof(e->page_url),"%s",urls[i]);
 }
 return count;
}
