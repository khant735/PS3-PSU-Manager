#include "ps2_archive_icons.h"
#include <assert.h>
#include <string.h>
int main(void){
 unsigned char data[1024]={0}; ps2_archive_icon_view view;
 ps2_max_entry max[2]={{"icon.sys",452,0},{"custom.icn",8,600}};
 ps2_cbs_entry cbs[2]={{"icon.sys",452,0,{0},{0},0},{"custom.icn",8,0,{0},{0},600}};
 memcpy(data,"PS2D",4);memcpy(data+260,"custom.icn",11);memcpy(data+600,"ICONTEST",8);
 assert(ps2_icons_view_max(data,sizeof(data),max,2,&view)==0);
 assert(view.data==data+600 && view.size==8 && !memcmp(view.data,"ICONTEST",8));
 assert(ps2_icons_view_cbs(data,sizeof(data),cbs,2,&view)==0 && view.data==data+600);
 max[1].data_offset=1020;
 assert(ps2_icons_view_max(data,sizeof(data),max,2,&view)==-2 && view.data==0);
 return 0;
}
