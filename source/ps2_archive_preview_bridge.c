#include "ps2_archive_preview_bridge.h"
#include "ps2_archive_icons.h"
#include <string.h>
static int dispatch(const uint8_t *decoded,size_t length,const void *entries,
 size_t count,int cbs,const char *archive,ps2_archive_preview_callback callback,void *context){
 ps2_archive_icon_manifest manifest;
 ps2_archive_icon_view view;
 int result;
 if(!callback || !archive || !*archive) return -1;
 if(cbs) {
  result=ps2_icons_from_cbs(decoded,length,(const ps2_cbs_entry*)entries,count,&manifest);
  if(result) return result;
  if(!manifest.valid_icon_sys || !manifest.has_view_icon) return 0;
  result=ps2_icons_view_cbs(decoded,length,(const ps2_cbs_entry*)entries,count,&view);
 }else{
  result=ps2_icons_from_max(decoded,length,(const ps2_max_entry*)entries,count,&manifest);
  if(result) return result;
  if(!manifest.valid_icon_sys || !manifest.has_view_icon) return 0;
  result=ps2_icons_view_max(decoded,length,(const ps2_max_entry*)entries,count,&view);
 }
 if(result) return result;
 if(!view.data || view.size<20) return 0;
 /* Existing ICN renderer expects a little-endian 0x00010000 signature. */
 if(view.data[0]!=0 || view.data[1]!=0 || view.data[2]!=1 || view.data[3]!=0) return 0;
 return callback(view.data,view.size,archive,manifest.view,context)>0?1:0;
}
int ps2_archive_preview_max(const uint8_t *d,size_t n,const ps2_max_entry *e,size_t c,const char *a,ps2_archive_preview_callback cb,void *ctx){return dispatch(d,n,e,c,0,a,cb,ctx);}
int ps2_archive_preview_cbs(const uint8_t *d,size_t n,const ps2_cbs_entry *e,size_t c,const char *a,ps2_archive_preview_callback cb,void *ctx){return dispatch(d,n,e,c,1,a,cb,ctx);}
