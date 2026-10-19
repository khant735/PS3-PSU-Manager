#ifndef PS2IODB_CACHE_H
#define PS2IODB_CACHE_H
#ifdef __cplusplus
extern "C" {
#endif
/* Read-only lookup: never edits the save or creates a PSU. */
typedef struct {const char *directory; const char *game; const char *slug; const char *model; const char *region; int exact;} ps2iodb_match;
/* Returns 1 for exact, 2 for cross-region candidate, 0 for no match. */
int ps2iodb_lookup(const char *save_directory, ps2iodb_match *result);
/* Download assets into an isolated cache; caller must initialize networking.
   curl TLS trust is mandatory. Returns 0 on success. */
int ps2iodb_cache_fetch(const ps2iodb_match *match,const char *cache_root,const char *ca_bundle);
#ifdef __cplusplus
}
#endif
#endif
