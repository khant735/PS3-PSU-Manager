#include "ps2_archive_icons.h"
#include <assert.h>
#include <string.h>
int main(void){
 unsigned char d[1024]={0};ps2_archive_icon_manifest m;
 ps2_max_entry e[2]={{"icon.sys",964,0},{"view.icn",16,1000}};
 memcpy(d,"PS2D",4);memcpy(d+260,"view.icn",9);
 assert(ps2_icons_from_max(d,sizeof(d),e,2,&m)==0);
 assert(m.valid_icon_sys && m.has_view_icon && !m.has_copy_icon);
 e[1].data_offset=1010;assert(ps2_icons_from_max(d,sizeof(d),e,2,&m)==-2);
 e[1].data_offset=1000;memcpy(d+260,"../bad",7);
 assert(ps2_icons_from_max(d,sizeof(d),e,2,&m)==0 && !m.valid_icon_sys);
 ps2_cbs_entry c[2]={{"icon.sys",964,0,{0},{0},0},{"view.icn",16,0,{0},{0},1000}};
 memcpy(d+260,"view.icn",9);
 assert(ps2_icons_from_cbs(d,sizeof(d),c,2,&m)==0 && m.has_view_icon);
 return 0;
}
