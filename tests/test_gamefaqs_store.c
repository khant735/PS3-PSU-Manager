#include "../source/gamefaqs_index_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv){
 assert(argc==2);
 char path[512];snprintf(path,sizeof(path),"%s/index.tsv",argv[1]);
 remove(path);
 gfs_save_t e={0};
 strcpy(e.game,"Example Game");strcpy(e.region,"USA");strcpy(e.format,"PAGE");
 strcpy(e.description,"Unverified save detail");
 strcpy(e.page_url,"https://gamefaqs.gamespot.com/ps2/123456-example/saves/987");
 assert(gfs_merge_index_file(path,&e,1,8)==1);
 assert(gfs_merge_index_file(path,&e,1,8)==1);
 strcpy(e.page_url,"https://gamefaqs.gamespot.com/ps2/123456-example/saves/988");
 assert(gfs_merge_index_file(path,&e,1,8)==2);
 strcpy(e.download_url,"https://gamefaqs.gamespot.com/download/example.psu");
 assert(gfs_merge_index_file(path,&e,1,8)==-1);
 strcpy(e.download_url,"https://evil.example/payload.psu");
 assert(gfs_merge_index_file(path,&e,1,8)==-1);
 FILE *f=fopen(path,"rb");assert(f);
 char data[4096];size_t n=fread(data,1,sizeof(data)-1,f);fclose(f);data[n]=0;
 gfs_save_t rows[8];assert(gfs_parse_index(data,rows,8)==2);
 /* Verified file replaces a PAGE record without consuming another slot. */
 remove(path);
 memset(&e,0,sizeof(e));
 strcpy(e.game,"Example Game");strcpy(e.region,"USA");strcpy(e.format,"PAGE");
 strcpy(e.page_url,"https://gamefaqs.gamespot.com/ps2/123456-example/saves/987");
 assert(gfs_merge_index_file(path,&e,1,8)==1);
 strcpy(e.format,"PSU");
 strcpy(e.download_url,"https://gamefaqs.gamespot.com/files/example.psu");
 assert(gfs_merge_index_file(path,&e,1,8)==1);
 strcpy(e.format,"PAGE");e.download_url[0]=0;
 assert(gfs_merge_index_file(path,&e,1,8)==1);
 f=fopen(path,"rb");assert(f);
 n=fread(data,1,sizeof(data)-1,f);fclose(f);data[n]=0;
 assert(gfs_parse_index(data,rows,8)==1);
 assert(strcmp(rows[0].format,"PSU")==0);
 assert(rows[0].download_url[0]!=0);
 /* A malformed existing row must not be silently dropped on merge. */
 f=fopen(path,"wb");assert(f);
 assert(fputs("malformed\\trow\\n",f)>=0);assert(fclose(f)==0);
 assert(gfs_merge_index_file(path,&e,1,8)==-1);
 f=fopen(path,"rb");assert(f);
 n=fread(data,1,sizeof(data)-1,f);fclose(f);data[n]=0;
 assert(strcmp(data,"malformed\\trow\\n")==0);
 remove(path);
 puts("Persistent GameFAQs index merge OK");
 return 0;
}
