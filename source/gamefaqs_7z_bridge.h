#ifndef GAMEFAQS_7Z_BRIDGE_H
#define GAMEFAQS_7Z_BRIDGE_H
#include "gamefaqs_saves_index.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*gfs_7z_entry_callback)(const gfs_save_t *entry, void *user);
typedef struct {
  gfs_7z_entry_callback on_entry;
  void *user;
  unsigned accepted;
  unsigned rejected;
} gfs_7z_import_context;
/* Adapter for ps3_gfs_read_7z: validates each line using the existing
   six-column GameFAQs TSV parser, without allocating an entire TSV file. */
int gfs_7z_import_line(const char *line, size_t length, void *context);
#ifdef __cplusplus
}
#endif
#endif
