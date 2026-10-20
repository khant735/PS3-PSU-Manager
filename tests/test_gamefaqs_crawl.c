#include "../source/gamefaqs_crawl.h"
#include "../source/gamefaqs_saves_index.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int calls=0;
static int deny=0;
static int fetch(const char *url,char **out,size_t *length,void *ctx){
 (void)ctx;calls++;
 if(deny)return 403;
 const char *page=strstr(url,"/saves/987")
 ?"<a href='/files/example.psu'>PSU</a>"
 :"<a href='/ps2/123456-example/saves/987'>save</a><a href='/ps2/999999-other/saves/888'>other</a>";
 *length=strlen(page);*out=malloc(*length+1);assert(*out);
 memcpy(*out,page,*length+1);return 200;
}
static void release(char *p,void *ctx){(void)ctx;free(p);}
int main(int argc,char **argv){
 assert(argc==2);
 char path[512];snprintf(path,sizeof(path),"%s/crawl.tsv",argv[1]);remove(path);
 assert(gfs_crawl_listing("https://gamefaqs.gamespot.com/ps2/123456-example/saves",
 path,"Example","USA",8,fetch,release,NULL)==2);
 assert(calls==2);
 FILE *f=fopen(path,"rb");assert(f);
 char data[4096];size_t n=fread(data,1,sizeof(data)-1,f);fclose(f);data[n]=0;
 gfs_save_t entries[8];assert(gfs_parse_index(data,entries,8)>=1);
 assert(strstr(data,"example.psu")!=NULL);
 assert(strstr(data,"999999-other")==NULL);
 deny=1;
 assert(gfs_crawl_listing("https://gamefaqs.gamespot.com/ps2/123456-example/saves",
 path,"Example","USA",8,fetch,release,NULL)==-1);
 f=fopen(path,"rb");assert(f);
 n=fread(data,1,sizeof(data)-1,f);fclose(f);data[n]=0;
 assert(strstr(data,"example.psu")!=NULL);
 remove(path);puts("GameFAQs crawl OK");return 0;
}
