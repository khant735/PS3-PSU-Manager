#ifndef PS3PSU_PSU_H
#define PS3PSU_PSU_H

#include <stddef.h>
#include <stdint.h>

#define PSU_HEADER_SIZE 0x200u
#define PSU_DATA_ALIGN  0x400u
#define PSU_NAME_MAX    32u
#define PSU_ATTR_DIR    0x8427u
#define PSU_ATTR_FILE   0x8497u

typedef struct {
    uint8_t unused, sec, min, hour, day, month;
    uint16_t year;
} psu_time_t;

typedef struct {
    uint16_t attr;
    uint32_t size;
    psu_time_t ctime;
    psu_time_t mtime;
    uint64_t unknown2;
    char name[PSU_NAME_MAX + 1];
} psu_entry_t;

typedef int (*psu_list_cb)(const psu_entry_t *entry, size_t index, void *opaque);

int psu_list(const char *psu_path, psu_list_cb cb, void *opaque);
int psu_get_root(const char *psu_path, psu_entry_t *root_out);
int psu_extract_all(const char *psu_path, const char *dest_dir);
int psu_extract_one(const char *psu_path, const char *entry_name, const char *dest_path);
int psu_read_file(const char *psu_path, const char *entry_name, void **data_out, size_t *size_out);
int psu_create_from_dir(const char *src_dir, const char *psu_path, const char *save_name);
int psu_create_blank(const char *psu_path, const char *save_name);
int psu_create_blank_template(const char *psu_path, const char *save_name, const char *display_title);
int psu_create_blank_template_ex(const char *psu_path, const char *save_name, const char *display_title, const char *icon_text, const char *view_icon_base);
int psu_create_blank_template_ex2(const char *psu_path, const char *save_name, const char *game_name, const char *save_title, const char *icon_text, const char *view_icon_base);
int psu_add_or_replace(const char *psu_path, const char *src_file, const char *entry_name);
int psu_delete_entry(const char *psu_path, const char *entry_name);
int psu_rename_entry(const char *psu_path, const char *old_name, const char *new_name);
const char *psu_strerror(int code);

#endif
