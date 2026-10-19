#ifndef PS2_ARCHIVE_FILE_PREVIEW_H
#define PS2_ARCHIVE_FILE_PREVIEW_H
#include "ps2_archive_preview_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Read-only file adapter. 1=preview callback accepted, 0=no view icon,
 * negative=I/O, unsupported or corrupt archive. Never writes to disk. */
int ps2_archive_preview_file(const char *path, ps2_archive_preview_callback cb, void *context);
#ifdef __cplusplus
}
#endif
#endif
