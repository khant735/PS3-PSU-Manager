#include "ps2_save_archive_header.h"
#include <string.h>
#include <stdint.h>
static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static int copy_name(char *out, const unsigned char *p, size_t n) {
    size_t i;
    for (i=0;i<n && p[i];i++) {
        if (p[i]=='/' || p[i]=='\\' || p[i]<32 || p[i]==127) return -1;
        out[i]=(char)p[i];
    }
    if (!i || i==n) return -1;
    out[i]=0;
    if (!strcmp(out,".") || !strcmp(out,"..")) return -1;
    return 0;
}
int ps2_archive_read_header(const unsigned char *b, size_t n, ps2_archive_header *out) {
    ps2_archive_header t;
    if (!b || !out) return -1;
    memset(&t,0,sizeof(t));
    t.format=ps2_save_detect_header(b,n);
    if (t.format==PS2_SAVE_MAX) {
        /* Apollo ps2mc.h: 12 magic + 4 CRC + 32 directory + 32 icon + 3x u32 */
        if (n<92 || copy_name(t.directory,b+16,32) || copy_name(t.icon_sys_name,b+48,32)) return -2;
        t.compressed_bytes=le32(b+80);
        t.entry_count=le32(b+84);
        t.unpacked_bytes=le32(b+88);
        /* MAX LZARI stream begins at sizeof(header)-4; 4 bytes overlap the final field. */
        t.payload_offset=88;
        if (!t.compressed_bytes || !t.unpacked_bytes || !t.entry_count ||
            t.compressed_bytes>n-t.payload_offset || t.unpacked_bytes>64u*1024u*1024u ||
            t.entry_count>65535u) return -2;
    } else if (t.format==PS2_SAVE_CBS) {
        /* Apollo ps2mc.h CBS header: 4 magic, four u32, 32-byte save directory,
           16 bytes timestamps, 8 bytes mode/unknown, 16 unknown, 72 title, 132 description. */
        const size_t header_size=296;
        if (n<header_size || le32(b+8)!=header_size || copy_name(t.directory,b+20,32)) return -2;
        t.unpacked_bytes=le32(b+12);
        t.compressed_bytes=le32(b+16);
        t.payload_offset=header_size;
        if (!t.unpacked_bytes || t.unpacked_bytes>64u*1024u*1024u || n<=header_size) return -2;
        /* CBS compressed-size fields are unreliable in existing implementations. */
    } else return -2;
    *out=t;
    return 0;
}
