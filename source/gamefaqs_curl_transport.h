#ifndef GAMEFAQS_CURL_TRANSPORT_H
#define GAMEFAQS_CURL_TRANSPORT_H
#include "gamefaqs_crawl.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Uses the application's installed CA bundle. Caller must initialise
 * libcurl/networking before invoking the crawler. */
int gfs_curl_fetch(const char *url,char **html,size_t *length,void *ctx);
void gfs_curl_release(char *html,void *ctx);
/* Run the bounded crawler with the native HTTPS transport. */
int gfs_crawl_listing_https(const char *listing_url,const char *index_path,
 const char *game,const char *region,int maximum_entries);
#ifdef __cplusplus
}
#endif
#endif
