#ifndef PS2_ARCHIVE_PREVIEW_BRIDGE_H
#define PS2_ARCHIVE_PREVIEW_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#include "ps2_max_entries.h"
#include "ps2_cbs_entries.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Invokes the existing renderer callback with validated, caller-owned ICN bytes.
 * The callback must consume/copy the data before returning; no ownership transfer.
 * Returns 1 for a successful preview, 0 when no valid view icon, negative on error.
 */
typedef int (*ps2_archive_preview_callback)(const void *bytes,size_t length,
                                            const char *archive,const char *entry,
                                            void *context);
int ps2_archive_preview_max(const uint8_t *decoded,size_t length,
    const ps2_max_entry *entries,size_t count,const char *archive,
    ps2_archive_preview_callback callback,void *context);
int ps2_archive_preview_cbs(const uint8_t *decoded,size_t length,
    const ps2_cbs_entry *entries,size_t count,const char *archive,
    ps2_archive_preview_callback callback,void *context);
#ifdef __cplusplus
}
#endif
#endif
