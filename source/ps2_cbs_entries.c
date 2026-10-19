#include "ps2_cbs_entries.h"
#include <string.h>
static uint32_t read_le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static int safe_name(char *dst, const unsigned char *src) {
    size_t i;
    for (i=0; i<32 && src[i]; ++i) {
        unsigned char c=src[i];
        if (c<32 || c==127 || c=='/' || c=='\\' || c==':') return -1;
        dst[i]=(char)c;
    }
    if (!i || i==32) return -1;
    dst[i]=0;
    if (!strcmp(dst,".") || !strcmp(dst,"..")) return -1;
    return 0;
}
int ps2_cbs_list_entries(const unsigned char *decoded, size_t length,
                         ps2_cbs_entry *entries, size_t capacity, size_t *count) {
    size_t pos=0, n=0;
    if (count) *count=0;
    if (!decoded || !count || (capacity && !entries)) return -1;
    while (pos<length) {
        ps2_cbs_entry e;
        uint32_t sz;
        /* Apollo's cbsEntry_t: created(8), modified(8), length(4), mode(4),
           unknown(8), filename(32) = 64 bytes. */
        if (length-pos<64) return -2;
        memset(&e,0,sizeof(e));
        sz=read_le32(decoded+pos+16);
        e.mode=read_le32(decoded+pos+20);
        memcpy(e.created,decoded+pos,8);
        memcpy(e.modified,decoded+pos+8,8);
        if (safe_name(e.name,decoded+pos+32)) return -2;
        pos+=64;
        if (sz>length-pos) return -2;
        e.size=sz;
        e.data_offset=pos;
        if (n<capacity) entries[n]=e;
        ++n;
        pos+=sz;
    }
    *count=n;
    return n>capacity ? -3 : 0;
}
