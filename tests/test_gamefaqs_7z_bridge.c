#include "gamefaqs_7z_bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int count_entry(const gfs_save_t *e,void *user){
 int *count=(int*)user;
 assert(strcmp(e->game,"Summer")==0);
 assert(strcmp(e->format,"PAGE")==0);
 ++*count;
 return 0;
}
int main(void){
 int count=0;
 gfs_7z_import_context ctx={count_entry,&count,0,0};
 const char *valid="Summer\tJapan\tPAGE\tGameFAQs listing\thttps://gamefaqs.gamespot.com/ps2/931604-summer-/saves\t";
 assert(gfs_7z_import_line(valid,strlen(valid),&ctx)==0);
 assert(ctx.accepted==1&&count==1);
 assert(gfs_7z_import_line("# heading",9,&ctx)==0);
 assert(gfs_7z_import_line("invalid",7,&ctx)!=0);
 assert(ctx.rejected==1);
 puts("GameFAQs 7z row adapter: OK");
 return 0;
}
