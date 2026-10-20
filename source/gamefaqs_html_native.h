#ifndef GAMEFAQS_HTML_NATIVE_H
#define GAMEFAQS_HTML_NATIVE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "gamefaqs_saves_index.h"
/* Extract explicit download links from an already retrieved, NUL-terminated
 * HTML page. No scraping, bypass or guessing of download endpoints. */
int gfs_extract_detail_html(const char *html, const char *page_url,
                            const char *game, const char *region,
                            gfs_save_t *out, int capacity);
/* Extract and deduplicate verified save-page URLs from catalogue HTML. */
int gfs_discover_save_links(const char *html, char (*urls)[384], int capacity);
/* Produce PAGE-only records for verified save-detail page paths. */
int gfs_index_save_pages(const char *html,const char *game,const char *region,
                         gfs_save_t *out,int capacity);
#ifdef __cplusplus
}
#endif
#endif
