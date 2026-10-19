#include "zip_extract.h"

#include <cstdio>
#include <cstring>

int main(int argc, char **argv) {
    if (argc != 4) return 64;
    int mode = ZIP_EXTRACT_WRITE;
    if (std::strcmp(argv[1], "check") == 0) mode = ZIP_EXTRACT_PREFLIGHT;
    else if (std::strcmp(argv[1], "overwrite") == 0) mode = ZIP_EXTRACT_OVERWRITE;
    zip_extract_result_t result;
    int rc = zip_extract_archive(argv[2], argv[3], mode, &result);
    std::printf("rc=%d files=%u dirs=%u conflicts=%u bytes=%llu error=%s\n",
                rc, result.files, result.directories, result.conflicts,
                result.total_bytes, result.error);
    if (rc == ZIP_EXTRACT_NEEDS_OVERWRITE) return 2;
    return rc == ZIP_EXTRACT_OK ? 0 : 1;
}
