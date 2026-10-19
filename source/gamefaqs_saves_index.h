#ifndef GAMEFAQS_SAVES_INDEX_H
#define GAMEFAQS_SAVES_INDEX_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#define GFS_MAX_ENTRIES 8192
typedef struct {
 char game[128],region[48],format[24],description[192],page_url[384],download_url[512];
} gfs_save_t;
/* TSV columns: game, region, format, description, GameFAQs page, direct download URL.
   Direct URL may be empty: then only the source page is displayed.
   Rejects non-GameFAQs pages and untrusted download hosts. */
int gfs_parse_index(const char *data,gfs_save_t *out,int capacity);
int gfs_bucket(const char *game); /* 0 = 0-9, 1..26 = A..Z */
int gfs_download_allowed(const char *url);
int gfs_parse_save_page_ids(const char *url, unsigned *game_id, unsigned *save_id); /* save_id=0 for listing */
#ifdef __cplusplus
}
#endif
#endif
