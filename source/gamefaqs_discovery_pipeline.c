#include "gamefaqs_discovery_pipeline.h"
#include "gamefaqs_html_native.h"
#include "gamefaqs_index_store.h"
#include <stdlib.h>
#include <string.h>

/* Bounded batch to avoid allocating the entire catalogue per HTML page. */
int gfs_ingest_html(const char *index_path,const char *html,const char *detail_url,
                    const char *game,const char *region,int maximum_entries){
 if(!index_path||!html||!game||!region||maximum_entries<1||
    maximum_entries>GFS_MAX_ENTRIES)return -1;
 const int batch=64;
 gfs_save_t *records=(gfs_save_t*)calloc(batch,sizeof(*records));
 if(!records)return -1;
 int n;
 if(detail_url){
  n=gfs_extract_detail_html(html,detail_url,game,region,records,batch);
 }else{
  n=gfs_index_save_pages(html,game,region,records,batch);
 }
 if(n<0){free(records);return -1;}
 /* Do not replace an existing catalogue if no matching records were found.
  * A blocked or redesigned page must not erase earlier indexed saves. */
 if(!n){free(records);return 0;}
 int merged=gfs_merge_index_file(index_path,records,n,maximum_entries);
 free(records);
 return merged;
}
