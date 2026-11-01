#ifndef PS3_GFS_7Z_H
#define PS3_GFS_7Z_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*ps3_gfs_line_fn)(const char *line, size_t length, void *context);
/* Read bounded TSV members from a non-solid 7z archive; zero on success. */
int ps3_gfs_read_7z(const char *archive, size_t max_entry_bytes,
                    ps3_gfs_line_fn callback, void *context);
#ifdef __cplusplus
}
#endif
#endif
