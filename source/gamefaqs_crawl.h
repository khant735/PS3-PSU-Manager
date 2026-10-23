#ifndef GAMEFAQS_CRAWL_H
#define GAMEFAQS_CRAWL_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Transport supplied by caller. Must return HTTP status and allocated,
 * NUL-terminated HTML. Caller releases it through release(). */
typedef int (*gfs_fetch_fn)(const char *url,char **html,size_t *length,void *ctx);
typedef void (*gfs_release_fn)(char *html,void *ctx);
/* Crawl one explicitly supplied listing page and its discovered detail pages.
 * Never guesses pagination, retries blocked responses or follows redirects.
 * Returns number of successfully processed pages, -1 on failure. */
int gfs_crawl_listing(const char *listing_url,const char *index_path,
 const char *game,const char *region,int maximum_entries,
 gfs_fetch_fn fetch,gfs_release_fn release,void *ctx);
/* Reuse an already fetched listing, avoiding a second network request. */
int gfs_crawl_listing_html(const char *listing_url,const char *html,const char *index_path,
 const char *game,const char *region,int maximum_entries,
 gfs_fetch_fn fetch,gfs_release_fn release,void *ctx);
#ifdef __cplusplus
}
#endif
#endif
