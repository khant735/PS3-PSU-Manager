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
#ifdef __cplusplus
}
#endif
#endif
