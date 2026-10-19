#ifndef PS2_ARCHIVE_ICONS_H
#define PS2_ARCHIVE_ICONS_H
#include <stddef.h>
#include <stdint.h>
#include "ps2_max_entries.h"
#include "ps2_cbs_entries.h"
#ifdef __cplusplus
extern "C" {
#endif
/* A read-only icon manifest. No renderer or archive files are modified. */
typedef struct {
    int has_icon_sys;
    int valid_icon_sys;
    int has_view_icon;
    int has_copy_icon;
    int has_delete_icon;
    char view[65], copy[65], delete_name[65];
} ps2_archive_icon_manifest;
int ps2_icons_from_max(const uint8_t *decoded, size_t length,
    const ps2_max_entry *entries, size_t count, ps2_archive_icon_manifest *out);
int ps2_icons_from_cbs(const uint8_t *decoded, size_t length,
    const ps2_cbs_entry *entries, size_t count, ps2_archive_icon_manifest *out);
/* Zero-copy view into caller-owned decoded archive; never free view.data. */
typedef struct { const uint8_t *data; size_t size; } ps2_archive_icon_view;
/* Only returns the icon named by a validated icon.sys manifest. */
int ps2_icons_view_max(const uint8_t *decoded, size_t length,
    const ps2_max_entry *entries, size_t count, ps2_archive_icon_view *view);
int ps2_icons_view_cbs(const uint8_t *decoded, size_t length,
    const ps2_cbs_entry *entries, size_t count, ps2_archive_icon_view *view);
#ifdef __cplusplus
}
#endif
#endif
