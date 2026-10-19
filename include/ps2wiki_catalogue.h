#ifndef PS2WIKI_CATALOGUE_H
#define PS2WIKI_CATALOGUE_H
#include <stddef.h>
#define PS2WIKI_MAX_ITEMS 128
#define PS2WIKI_CATEGORY_COUNT 6
#define PS2WIKI_TITLE 128
#define PS2WIKI_VERSION 48
#define PS2WIKI_DETAIL_URL 384
#define PS2WIKI_DOWNLOAD_URL 1024

typedef struct {
 int category;
 char title[PS2WIKI_TITLE];
 char version[PS2WIKI_VERSION];
 char detail_url[PS2WIKI_DETAIL_URL];
 char download_url[PS2WIKI_DOWNLOAD_URL];
} ps2wiki_entry_t;

/* These functions are deliberately platform-independent for host tests. */
int ps2wiki_valid_detail_url(const char *url, int category);
int ps2wiki_valid_filen_url(const char *url);
int ps2wiki_valid_direct_psu(const char *url);
int ps2wiki_parse_category(const char *html, int category, ps2wiki_entry_t *out, int cap);
int ps2wiki_parse_detail(const char *html, char *url, size_t cap);
#endif
