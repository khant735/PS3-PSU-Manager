#include "ps2_archive_icons.h"
#include <string.h>
/* icon.sys: PS2D magic, three 64-byte filenames at offsets 260,324,388. */
static int safe_name(const char *s, size_t cap) {
    size_t i;
    for (i=0;i<cap && s[i];i++) {
        unsigned char c=(unsigned char)s[i];
        if (c<32 || c==127 || c=='/' || c=='\\' || c==':') return 0;
    }
    return i>0 && i<cap && strcmp(s,".") && strcmp(s,"..");
}
static int field(char dest[65],const uint8_t *p) {
    size_t n=0;
    while(n<64 && p[n]) n++;
    if(n==64) return 0;
    memcpy(dest,p,n);dest[n]=0;
    return safe_name(dest,65);
}
/* PS3 PPU stack safety: iterate directly, avoiding 4096-entry local arrays. */
static int scan(const uint8_t *d,size_t len,const void *entries,size_t count,int cbs,
                ps2_archive_icon_manifest *out,ps2_archive_icon_view *view) {
    size_t i,j;const char *name;size_t off;uint32_t sz;
    if (!out || (count && !entries) || (!d && len) || count>4096) return -1;
    memset(out,0,sizeof(*out));
    if(view) {view->data=NULL;view->size=0;}
    for(i=0;i<count;i++) {
        if(cbs){const ps2_cbs_entry *e=&((const ps2_cbs_entry*)entries)[i];name=e->name;off=e->data_offset;sz=e->size;}
        else {const ps2_max_entry *e=&((const ps2_max_entry*)entries)[i];name=e->name;off=e->data_offset;sz=e->size;}
        if(!memchr(name,0,33)) return -2;
        if(off>len || sz>len-off) return -2;
    }
    for(i=0;i<count;i++) {
        if(cbs){const ps2_cbs_entry *e=&((const ps2_cbs_entry*)entries)[i];name=e->name;off=e->data_offset;sz=e->size;}
        else {const ps2_max_entry *e=&((const ps2_max_entry*)entries)[i];name=e->name;off=e->data_offset;sz=e->size;}
        if(strcmp(name,"icon.sys")) continue;
        out->has_icon_sys=1;
        if(sz<452 || memcmp(d+off,"PS2D",4)) return 0;
        if(!field(out->view,d+off+260)) return 0;
        out->valid_icon_sys=1;
        (void)field(out->copy,d+off+324);
        (void)field(out->delete_name,d+off+388);
        for(j=0;j<count;j++) {
            const char *n=cbs?((const ps2_cbs_entry*)entries)[j].name:((const ps2_max_entry*)entries)[j].name;
            if(!strcmp(n,out->view)) {
                out->has_view_icon=1;
                if(view) {
                    size_t pos=cbs?((const ps2_cbs_entry*)entries)[j].data_offset:((const ps2_max_entry*)entries)[j].data_offset;
                    uint32_t size=cbs?((const ps2_cbs_entry*)entries)[j].size:((const ps2_max_entry*)entries)[j].size;
                    view->data=d+pos;view->size=size;
                }
            }
            if(out->copy[0] && !strcmp(n,out->copy))out->has_copy_icon=1;
            if(out->delete_name[0] && !strcmp(n,out->delete_name))out->has_delete_icon=1;
        }
        return 0;
    }
    return 0;
}
int ps2_icons_from_max(const uint8_t *d,size_t n,const ps2_max_entry *e,size_t c,ps2_archive_icon_manifest *o){return scan(d,n,e,c,0,o,NULL);}
int ps2_icons_from_cbs(const uint8_t *d,size_t n,const ps2_cbs_entry *e,size_t c,ps2_archive_icon_manifest *o){return scan(d,n,e,c,1,o,NULL);}
int ps2_icons_view_max(const uint8_t *d,size_t n,const ps2_max_entry *e,size_t c,ps2_archive_icon_view *v){ps2_archive_icon_manifest m;if(!v)return -1;return scan(d,n,e,c,0,&m,v);}
int ps2_icons_view_cbs(const uint8_t *d,size_t n,const ps2_cbs_entry *e,size_t c,ps2_archive_icon_view *v){ps2_archive_icon_manifest m;if(!v)return -1;return scan(d,n,e,c,1,&m,v);}
