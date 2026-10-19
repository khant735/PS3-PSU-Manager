#ifndef PS2HOMEBREW_CATALOGUE_H
#define PS2HOMEBREW_CATALOGUE_H
#ifdef __cplusplus
extern "C" {
#endif
#define PS2HB_MAX_ITEMS 256
#define PS2HB_MAX_ASSETS 16
#define PS2HB_URL_SIZE 1024
#define PS2HB_ASSET_PSU 1
#define PS2HB_ASSET_ZIP 2
typedef struct { const char *name; const char *path; } ps2hb_category_t;
typedef struct { int category; char title[128]; char detail_url[384]; } ps2hb_entry_t;
typedef struct { int kind; char title[128]; char label[96]; char url[PS2HB_URL_SIZE]; } ps2hb_asset_t;
extern const ps2hb_category_t ps2hb_categories[8];
int ps2hb_valid_category_url(const char *url, int category);
int ps2hb_valid_entry_url(const char *url);
int ps2hb_asset_kind_from_url(const char *url);
int ps2hb_requires_verified_tls(const char *url);
int ps2hb_redirect_allowed(const char *url);
int ps2hb_parse_category(const char *html, int category, ps2hb_entry_t *out, int cap);
int ps2hb_parse_entry(const char *html, const char *title, ps2hb_asset_t *out, int cap);
int ps2hb_validate_zip_file(const char *path);
#ifdef __cplusplus
}
#endif
#endif
