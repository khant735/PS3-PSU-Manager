#include "zip_extract.h"

#include <zlib.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

static const unsigned int ZIP_MAX_ENTRIES = 2048;
static const unsigned int ZIP_MAX_NAME = 1023;
static const unsigned long long ZIP_MAX_ENTRY_SIZE = 512ull * 1024ull * 1024ull;
static const unsigned long long ZIP_MAX_TOTAL_SIZE = 1024ull * 1024ull * 1024ull;
static const unsigned int ZIP_IO_CHUNK = 32768;
static const unsigned int ZIP_EOCD_MAX = 22 + 65535;

struct ZipEntry {
    char name[ZIP_MAX_NAME + 1];
    unsigned int crc;
    unsigned int compressed_size;
    unsigned int uncompressed_size;
    unsigned int local_offset;
    unsigned short flags;
    unsigned short method;
    int is_directory;
};

struct ZipArchive {
    FILE *file;
    ZipEntry *entries;
    unsigned int count;
    unsigned int central_offset;
    unsigned int central_size;
    unsigned long long file_size;
};

static unsigned short le16(const unsigned char *p) {
    return (unsigned short)(p[0] | ((unsigned short)p[1] << 8));
}

static unsigned int le32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static int seek_to(FILE *f, unsigned long long offset) {
    if (offset > 0x7FFFFFFFull) return 0;
    return fseek(f, (long)offset, SEEK_SET) == 0;
}

static int read_exact(FILE *f, void *data, size_t size) {
    return size == 0 || fread(data, 1, size, f) == size;
}

static int zip_lstat(const char *path, struct stat *st) {
#if defined(__PPC__)
    /* The PSL1GHT/newlib PPU headers expose stat() but omit the lstat() wrapper.
       ZIP-originated symlinks are independently rejected from their external
       attributes before any destination path is written. */
    return stat(path, st);
#else
    return lstat(path, st);
#endif
}

static int fail(zip_extract_result_t *r, const char *message) {
    if (r) snprintf(r->error, sizeof(r->error), "%s", message ? message : "ZIP error");
    return -1;
}

static int valid_name(char *name, size_t length, int *is_directory) {
    if (!name || !length || length > ZIP_MAX_NAME) return 0;
    if (memchr(name, 0, length)) return 0;
    for (size_t i = 0; i < length; ++i) if (name[i] == '\\') name[i] = '/';
    name[length] = 0;
    *is_directory = name[length - 1] == '/';
    if (*is_directory) name[--length] = 0;
    if (!length) return *is_directory;
    if (name[0] == '/' || name[0] == '\\') return 0;
    size_t component_start = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (name[i] == '/' || name[i] == 0) {
            size_t component_length = i - component_start;
            if (!component_length) return 0;
            if ((component_length == 1 && name[component_start] == '.') ||
                (component_length == 2 && name[component_start] == '.' && name[component_start + 1] == '.')) return 0;
            for (size_t j = component_start; j < i; ++j) {
                unsigned char c = (unsigned char)name[j];
                if (c < 0x20 || c == ':' || c == 0x7F) return 0;
            }
            component_start = i + 1;
        }
    }
    return 1;
}

static int same_name_folded(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++, cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb) return 0;
    }
    return *a == *b;
}

static int path_is_parent_folded(const char *parent, const char *child) {
    size_t n = strlen(parent);
    if (strlen(child) <= n || child[n] != '/') return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char a = (unsigned char)parent[i], b = (unsigned char)child[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

static void archive_close(ZipArchive *a) {
    if (!a) return;
    if (a->file) fclose(a->file);
    free(a->entries);
    memset(a, 0, sizeof(*a));
}

static int archive_open(const char *path, ZipArchive *a, zip_extract_result_t *r) {
    memset(a, 0, sizeof(*a));
    if (!path || !path[0]) return fail(r, "ZIP filename is empty");
    a->file = fopen(path, "rb");
    if (!a->file) return fail(r, "Cannot open ZIP file");
    if (fseek(a->file, 0, SEEK_END) != 0) { archive_close(a); return fail(r, "Cannot seek ZIP file"); }
    long end = ftell(a->file);
    if (end < 22 || (unsigned long long)end > 0x7FFFFFFFull) { archive_close(a); return fail(r, "ZIP size is unsupported"); }
    a->file_size = (unsigned long long)end;
    unsigned int tail_size = (unsigned int)(a->file_size < ZIP_EOCD_MAX ? a->file_size : ZIP_EOCD_MAX);
    unsigned char *tail = (unsigned char *)malloc(tail_size);
    if (!tail) { archive_close(a); return fail(r, "Not enough memory for ZIP directory"); }
    if (!seek_to(a->file, a->file_size - tail_size) || !read_exact(a->file, tail, tail_size)) {
        free(tail); archive_close(a); return fail(r, "Cannot read ZIP directory");
    }
    int eocd = -1;
    if (tail_size >= 22) {
        for (int i = (int)tail_size - 22; i >= 0; --i) {
            if (le32(tail + i) == 0x06054B50u) {
                unsigned int comment = le16(tail + i + 20);
                if ((unsigned int)i + 22u + comment == tail_size) { eocd = i; break; }
            }
        }
    }
    if (eocd < 0) { free(tail); archive_close(a); return fail(r, "ZIP end directory is missing or malformed"); }
    const unsigned char *endrec = tail + eocd;
    unsigned short disk = le16(endrec + 4), central_disk = le16(endrec + 6);
    unsigned short disk_count = le16(endrec + 8), count = le16(endrec + 10);
    unsigned int central_size = le32(endrec + 12), central_offset = le32(endrec + 16);
    unsigned long long eocd_offset = a->file_size - tail_size + (unsigned int)eocd;
    free(tail);
    if (disk || central_disk || disk_count != count) { archive_close(a); return fail(r, "Multi-disk ZIP files are not supported"); }
    if (count == 0xFFFFu || central_size == 0xFFFFFFFFu || central_offset == 0xFFFFFFFFu) {
        archive_close(a); return fail(r, "ZIP64 archives are not supported yet");
    }
    if (count > ZIP_MAX_ENTRIES) { archive_close(a); return fail(r, "ZIP contains too many entries"); }
    if ((unsigned long long)central_offset + central_size > eocd_offset) {
        archive_close(a); return fail(r, "ZIP central directory is outside the file");
    }
    a->count = count; a->central_offset = central_offset; a->central_size = central_size;
    a->entries = count ? (ZipEntry *)calloc(count, sizeof(ZipEntry)) : NULL;
    if (count && !a->entries) { archive_close(a); return fail(r, "Not enough memory for ZIP entries"); }
    if (!seek_to(a->file, central_offset)) { archive_close(a); return fail(r, "Cannot seek ZIP central directory"); }
    unsigned long long total_size = 0;
    for (unsigned int i = 0; i < count; ++i) {
        unsigned char h[46];
        if (!read_exact(a->file, h, sizeof(h)) || le32(h) != 0x02014B50u) {
            archive_close(a); return fail(r, "Malformed ZIP central directory entry");
        }
        ZipEntry *e = &a->entries[i];
        unsigned short made_by = le16(h + 4);
        e->flags = le16(h + 8); e->method = le16(h + 10);
        e->crc = le32(h + 16); e->compressed_size = le32(h + 20);
        e->uncompressed_size = le32(h + 24);
        unsigned short name_len = le16(h + 28), extra_len = le16(h + 30), comment_len = le16(h + 32);
        unsigned short entry_disk = le16(h + 34);
        unsigned int external_attr = le32(h + 38); e->local_offset = le32(h + 42);
        if (!name_len || name_len > ZIP_MAX_NAME || entry_disk != 0 ||
            e->compressed_size == 0xFFFFFFFFu || e->uncompressed_size == 0xFFFFFFFFu ||
            e->local_offset == 0xFFFFFFFFu) {
            archive_close(a); return fail(r, "ZIP64 or split entries are not supported");
        }
        if ((e->flags & (1u | 0x40u | 0x2000u)) != 0) {
            archive_close(a); return fail(r, "Encrypted ZIP entries are not supported");
        }
        if (e->method != 0 && e->method != 8) {
            archive_close(a); return fail(r, "ZIP method unsupported (use STORE or DEFLATE)");
        }
        if (e->method == 0 && (e->flags & 6u)) {
            archive_close(a); return fail(r, "Invalid compression flags for stored ZIP entry");
        }
        if (e->uncompressed_size > ZIP_MAX_ENTRY_SIZE ||
            total_size + e->uncompressed_size > ZIP_MAX_TOTAL_SIZE) {
            archive_close(a); return fail(r, "ZIP expands beyond the safety size limit");
        }
        total_size += e->uncompressed_size;
        if (!read_exact(a->file, e->name, name_len)) { archive_close(a); return fail(r, "Truncated ZIP filename"); }
        if (!valid_name(e->name, name_len, &e->is_directory)) {
            archive_close(a); return fail(r, "ZIP contains an unsafe path");
        }
        if ((made_by >> 8) == 3) {
            unsigned int mode = external_attr >> 16;
            unsigned int type = mode & 0170000u;
            if (type == 0120000u || (type && type != 0100000u && type != 0040000u)) {
                archive_close(a); return fail(r, "ZIP links or special files are not supported");
            }
            if (type == 0040000u) e->is_directory = 1;
            if (type == 0100000u && e->is_directory) { archive_close(a); return fail(r, "ZIP entry type is inconsistent"); }
        }
        if (e->is_directory && e->uncompressed_size != 0) {
            archive_close(a); return fail(r, "ZIP directory entry contains data");
        }
        for (unsigned int j = 0; j < i; ++j) {
            if (same_name_folded(e->name, a->entries[j].name)) {
                archive_close(a); return fail(r, "ZIP contains duplicate or case-colliding paths");
            }
            if ((!a->entries[j].is_directory && path_is_parent_folded(a->entries[j].name, e->name)) ||
                (!e->is_directory && path_is_parent_folded(e->name, a->entries[j].name))) {
                archive_close(a); return fail(r, "ZIP contains file/folder path conflicts");
            }
        }
        if (fseek(a->file, (long)extra_len + comment_len, SEEK_CUR) != 0) {
            archive_close(a); return fail(r, "Truncated ZIP entry metadata");
        }
    }
    long after_directory = ftell(a->file);
    if (after_directory < 0 || (unsigned long long)after_directory > (unsigned long long)central_offset + central_size) {
        archive_close(a); return fail(r, "ZIP central directory length is inconsistent");
    }
    return 0;
}

static int path_join(char *out, size_t cap, const char *base, const char *relative) {
    int n = snprintf(out, cap, "%s/%s", base, relative);
    return n >= 0 && (size_t)n < cap;
}

static int directory_ok(const char *path) {
    struct stat st;
    if (zip_lstat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode);
}

static int ensure_directory_tree(const char *path) {
    char tmp[2048]; size_t n = strlen(path);
    if (!n || n >= sizeof(tmp)) return 0;
    memcpy(tmp, path, n + 1);
    for (size_t i = 1; i <= n; ++i) {
        if (tmp[i] != '/' && tmp[i] != 0) continue;
        char saved = tmp[i]; tmp[i] = 0;
        if (tmp[0] && !directory_ok(tmp)) {
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) { tmp[i] = saved; return 0; }
            if (!directory_ok(tmp)) { tmp[i] = saved; return 0; }
        }
        tmp[i] = saved;
    }
    return directory_ok(tmp);
}

static int validate_destination(const char *dest, zip_extract_result_t *r) {
    if (!dest || !dest[0] || strlen(dest) >= 1900) return fail(r, "Invalid extraction folder");
    struct stat st;
    if (zip_lstat(dest, &st) != 0 || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
        return fail(r, "Extraction folder is missing or unsafe");
    return 0;
}

static int check_paths(ZipArchive *a, const char *dest, int overwrite, zip_extract_result_t *r) {
    char full[2048];
    struct stat archive_stat;
    if (fstat(fileno(a->file), &archive_stat) != 0) return fail(r, "Cannot inspect ZIP source file");
    for (unsigned int i = 0; i < a->count; ++i) {
        ZipEntry *e = &a->entries[i];
        if (!e->name[0]) continue;
        if (!path_join(full, sizeof(full), dest, e->name)) return fail(r, "Extracted path is too long");
        struct stat st;
        if (zip_lstat(full, &st) == 0) {
            if (S_ISLNK(st.st_mode)) return fail(r, "Destination contains a symbolic link");
            if (st.st_dev == archive_stat.st_dev && st.st_ino == archive_stat.st_ino)
                return fail(r, "ZIP contents cannot overwrite the source archive");
            if (e->is_directory) {
                if (!S_ISDIR(st.st_mode)) return fail(r, "A ZIP folder conflicts with an existing file");
            } else {
                if (!S_ISREG(st.st_mode)) return fail(r, "A ZIP file conflicts with a folder or special file");
                ++r->conflicts;
            }
        } else if (errno != ENOENT) return fail(r, "Cannot inspect extraction destination");
        char parent[2048];
        snprintf(parent, sizeof(parent), "%s", full);
        char *slash = strrchr(parent, '/');
        if (slash) {
            *slash = 0;
            /* Existing parent components must be real directories, never links. */
            char probe[2048]; snprintf(probe, sizeof(probe), "%s", parent);
            for (char *p = probe + 1; *p; ++p) if (*p == '/') {
                *p = 0; struct stat pst;
                if (zip_lstat(probe, &pst) == 0 && (!S_ISDIR(pst.st_mode) || S_ISLNK(pst.st_mode))) return fail(r, "Unsafe existing parent folder");
                *p = '/';
            }
            struct stat pst;
            if (zip_lstat(probe, &pst) == 0 && (!S_ISDIR(pst.st_mode) || S_ISLNK(pst.st_mode))) return fail(r, "Unsafe existing parent folder");
        }
    }
    if (r->conflicts && !overwrite) return ZIP_EXTRACT_NEEDS_OVERWRITE;
    return 0;
}

static int make_temp_path(const char *target, char *tmp, size_t cap, FILE **file) {
    int n = snprintf(tmp, cap, "%s.ps3zipXXXXXX", target);
    if (n < 0 || (size_t)n >= cap) return 0;
    int fd = mkstemp(tmp);
    if (fd < 0) return 0;
    *file = fdopen(fd, "wb");
    if (!*file) { close(fd); unlink(tmp); return 0; }
    return 1;
}

static int copy_stored(FILE *src, FILE *dst, ZipEntry *e, uLong *crc) {
    unsigned char buf[ZIP_IO_CHUNK];
    unsigned int remaining = e->compressed_size;
    unsigned long long produced = 0;
    while (remaining) {
        unsigned int amount = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        if (!read_exact(src, buf, amount)) return 0;
        remaining -= amount;
        if (fwrite(buf, 1, amount, dst) != amount) return 0;
        *crc = crc32(*crc, buf, amount);
        produced += amount;
    }
    return produced == e->uncompressed_size;
}

static int copy_deflated(FILE *src, FILE *dst, ZipEntry *e, uLong *crc) {
    unsigned char in[ZIP_IO_CHUNK], out[ZIP_IO_CHUNK];
    z_stream stream; memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return 0;
    unsigned int remaining = e->compressed_size;
    unsigned long long produced = 0;
    int ok = 1, ended = 0;
    while (ok && !ended) {
        if (stream.avail_in == 0 && remaining) {
            unsigned int amount = remaining > sizeof(in) ? sizeof(in) : remaining;
            if (!read_exact(src, in, amount)) { ok = 0; break; }
            remaining -= amount;
            stream.next_in = in; stream.avail_in = amount;
        }
        stream.next_out = out; stream.avail_out = sizeof(out);
        int zr = inflate(&stream, Z_NO_FLUSH);
        unsigned int have = (unsigned int)(sizeof(out) - stream.avail_out);
        if (have) {
            if (produced + have > e->uncompressed_size || fwrite(out, 1, have, dst) != have) { ok = 0; break; }
            produced += have; *crc = crc32(*crc, out, have);
        }
        if (zr == Z_STREAM_END) ended = 1;
        else if (zr != Z_OK || (!have && stream.avail_in == 0 && remaining == 0)) ok = 0;
    }
    if (stream.total_in != e->compressed_size || produced != e->uncompressed_size) ok = 0;
    inflateEnd(&stream);
    return ok;
}

static int extract_entry(ZipArchive *a, ZipEntry *e, unsigned int index,
                         const char *dest, zip_extract_result_t *r) {
    if (e->is_directory) {
        if (!e->name[0]) return 1;
        char dir[2048];
        if (!path_join(dir, sizeof(dir), dest, e->name) || !ensure_directory_tree(dir))
            return fail(r, "Cannot create ZIP folder");
        return 1;
    }
    char target[2048];
    if (!path_join(target, sizeof(target), dest, e->name)) return fail(r, "Extracted path is too long");
    char parent[2048]; snprintf(parent, sizeof(parent), "%s", target);
    char *slash = strrchr(parent, '/');
    if (!slash) return fail(r, "Invalid extracted filename");
    *slash = 0;
    if (!ensure_directory_tree(parent)) return fail(r, "Cannot create ZIP destination folder");
    if (!seek_to(a->file, e->local_offset)) return fail(r, "Cannot seek ZIP file data");
    unsigned char local[30];
    if (!read_exact(a->file, local, sizeof(local)) || le32(local) != 0x04034B50u)
        return fail(r, "Malformed ZIP local file header");
    unsigned short local_flags = le16(local + 6), local_method = le16(local + 8);
    unsigned short local_name_len = le16(local + 26), local_extra_len = le16(local + 28);
    if (local_flags != e->flags || local_method != e->method || local_name_len > ZIP_MAX_NAME)
        return fail(r, "ZIP local and central headers do not match");
    if (!(e->flags & 8u) &&
        (le32(local + 14) != e->crc || le32(local + 18) != e->compressed_size ||
         le32(local + 22) != e->uncompressed_size))
        return fail(r, "ZIP local sizes do not match the central directory");
    char local_name[ZIP_MAX_NAME + 1];
    if (!read_exact(a->file, local_name, local_name_len) || memcmp(local_name, e->name, local_name_len) != 0)
        return fail(r, "ZIP local filename does not match");
    if (fseek(a->file, local_extra_len, SEEK_CUR) != 0) return fail(r, "Truncated ZIP local metadata");
    long data_start = ftell(a->file);
    if (data_start < 0 || (unsigned long long)data_start + e->compressed_size > a->central_offset)
        return fail(r, "ZIP file data overlaps its directory");
    char tmp[2100]; FILE *out = NULL;
    if (!make_temp_path(target, tmp, sizeof(tmp), &out)) return fail(r, "Cannot create temporary extracted file");
    uLong crc = crc32(0L, Z_NULL, 0);
    int ok = e->method == 0 ? copy_stored(a->file, out, e, &crc) : copy_deflated(a->file, out, e, &crc);
    if (fclose(out) != 0) ok = 0;
    if (!ok || (unsigned int)crc != e->crc) {
        unlink(tmp); return fail(r, "ZIP data or CRC check failed");
    }
    if (chmod(tmp, 0666) != 0) {
        unlink(tmp); return fail(r, "Cannot set extracted file permissions");
    }
    if (rename(tmp, target) != 0) { unlink(tmp); return fail(r, "Cannot install extracted file"); }
    (void)index;
    return 1;
}

} // namespace

extern "C" int zip_extract_archive(const char *archive_path, const char *destination,
                                    int mode, zip_extract_result_t *result) {
    zip_extract_result_t local_result; memset(&local_result, 0, sizeof(local_result));
    zip_extract_result_t *r = result ? result : &local_result;
    memset(r, 0, sizeof(*r));
    if (mode < ZIP_EXTRACT_PREFLIGHT || mode > ZIP_EXTRACT_OVERWRITE)
        return fail(r, "Invalid ZIP extraction mode");
    if (validate_destination(destination, r) != 0) return -1;
    ZipArchive archive;
    if (archive_open(archive_path, &archive, r) != 0) return -1;
    for (unsigned int i = 0; i < archive.count; ++i) {
        if (archive.entries[i].is_directory) ++r->directories;
        else { ++r->files; r->total_bytes += archive.entries[i].uncompressed_size; }
    }
    int check = check_paths(&archive, destination, mode == ZIP_EXTRACT_OVERWRITE, r);
    if (check != 0) { archive_close(&archive); return check; }
    if (mode == ZIP_EXTRACT_PREFLIGHT) { archive_close(&archive); return ZIP_EXTRACT_OK; }
    for (unsigned int i = 0; i < archive.count; ++i) {
        int rc = extract_entry(&archive, &archive.entries[i], i, destination, r);
        if (rc < 0) { archive_close(&archive); return -1; }
    }
    archive_close(&archive);
    return ZIP_EXTRACT_OK;
}
