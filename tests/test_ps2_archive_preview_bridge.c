#include "ps2_archive_preview_bridge.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef struct {int calls;size_t size;char entry[65];} recorder;
static int preview(const void *bytes,size_t len,const char *archive,const char *entry,void *ctx){
 recorder *r=(recorder*)ctx;const uint8_t *p=(const uint8_t*)bytes;
 assert(!strcmp(archive,"sample.max"));assert(p[2]==1);r->calls++;r->size=len;
 strcpy(r->entry,entry);return 1;
}
int main(void){
 uint8_t data[600]={0};ps2_max_entry entries[2]={{"icon.sys",452,0},{"CUSTOM.ICN",32,500}};
 ps2_cbs_entry cbs[2]={0};recorder r={0};
 memcpy(data,"PS2D",4);memcpy(data+260,"CUSTOM.ICN",11);
 data[502]=1;
 assert(ps2_archive_preview_max(data,sizeof(data),entries,2,"sample.max",preview,&r)==1);
 assert(r.calls==1 && r.size==32 && !strcmp(r.entry,"CUSTOM.ICN"));
 memcpy(cbs[0].name,"icon.sys",9);cbs[0].size=452;cbs[0].data_offset=0;
 memcpy(cbs[1].name,"CUSTOM.ICN",11);cbs[1].size=32;cbs[1].data_offset=500;
 assert(ps2_archive_preview_cbs(data,sizeof(data),cbs,2,"sample.max",preview,&r)==1);
 assert(r.calls==2);
 data[502]=0;
 assert(ps2_archive_preview_max(data,sizeof(data),entries,2,"sample.max",preview,&r)==0);
 assert(r.calls==2);
 data[502]=1;entries[1].data_offset=590;
 assert(ps2_archive_preview_max(data,sizeof(data),entries,2,"sample.max",preview,&r)<0);
 assert(r.calls==2);
 return 0;
}
