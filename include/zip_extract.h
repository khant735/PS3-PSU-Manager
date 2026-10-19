#ifndef PS3PSU_ZIP_EXTRACT_H
#define PS3PSU_ZIP_EXTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ZIP_EXTRACT_OK = 0,
    ZIP_EXTRACT_NEEDS_OVERWRITE = 1,
    ZIP_EXTRACT_PREFLIGHT = 0,
    ZIP_EXTRACT_WRITE = 1,
    ZIP_EXTRACT_OVERWRITE = 2
};

typedef struct zip_extract_result {
    unsigned int files;
    unsigned int directories;
    unsigned int conflicts;
    unsigned long long total_bytes;
    char error[192];
} zip_extract_result_t;

/* Supports single-disk ZIP32 archives using STORE or DEFLATE.  PREFLIGHT is
   read-only; WRITE refuses existing files; OVERWRITE atomically replaces
   existing regular files after each entry has passed its CRC check. */
int zip_extract_archive(const char *archive_path, const char *destination,
                        int mode, zip_extract_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
