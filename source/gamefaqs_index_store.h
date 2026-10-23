#ifndef GAMEFAQS_INDEX_STORE_H
#define GAMEFAQS_INDEX_STORE_H
#include "gamefaqs_saves_index.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Merge validated entries into a six-column TSV. Writes through a temporary
 * file and renames on success; returns -1 without changing the old file on
 * validation, capacity, or write failure. Existing entries take precedence. */
int gfs_merge_index_file(const char *path,const gfs_save_t *incoming,int incoming_count,
                         int maximum_entries);
/* Last failure source line and errno; zero line means no recorded failure. */
int gfs_merge_error_line(void);
int gfs_merge_error_errno(void);
#ifdef __cplusplus
}
#endif
#endif
