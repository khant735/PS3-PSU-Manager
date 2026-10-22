#ifndef PS2_GAME_TITLES_H
#define PS2_GAME_TITLES_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Extract a candidate disc serial from a save-folder ID, without verifying the disc. */
int ps2_game_extract_serial(const char *save_directory, char serial_out[11]);
/* Returns 1 on match, 0 on no match, negative on I/O/argument error.
   Database is tab-separated: SERIAL<TAB>TITLE; no game files are changed. */
int ps2_game_title_lookup(const char *database_path, const char *save_directory,
                          char *title_out, size_t title_capacity);
/* Read-only lookup of sourced release date from the 8-column releases TSV.
   Returns 1 only for a nonempty date with known precision and source. */
int ps2_game_release_lookup(const char *database_path, const char *serial,
                            char *date_out, size_t date_cap,
                            char *precision_out, size_t precision_cap,
                            char *source_out, size_t source_cap);
#ifdef __cplusplus
}
#endif
#endif
