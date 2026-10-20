#include "../source/gamefaqs_discovery_pipeline.h"
#include "../source/gamefaqs_saves_index.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv){
 assert(argc==2);
 char path[512];snprintf(path,sizeof(path),"%s/pipeline.tsv",argv[1]);
 remove(path);
 const char *detail="https://gamefaqs.gamespot.com/ps2/123456-example/saves/987";
 const char *listing="<a href='/ps2/123456-example/saves/987'>Save</a>";
 assert(gfs_ingest_html(path,listing,NULL,"Example","USA",8)==1);
 const char *html="<a href='/files/example.psu'>Download PSU</a>";
 assert(gfs_ingest_html(path,html,detail,"Example","USA",8)==1);
 FILE *f=fopen(path,"rb");assert(f);
 char data[4096];size_t n=fread(data,1,sizeof(data)-1,f);fclose(f);data[n]=0;
 gfs_save_t rows[8];assert(gfs_parse_index(data,rows,8)==1);
 assert(strcmp(rows[0].format,"PSU")==0);
 assert(strstr(rows[0].download_url,"example.psu")!=NULL);
 assert(gfs_ingest_html(path,"<html>Access denied</html>",NULL,"Example","USA",8)==0);
 f=fopen(path,"rb");assert(f);n=fread(data,1,sizeof(data)-1,f);fclose(f);data[n]=0;
 assert(gfs_parse_index(data,rows,8)==1);
 remove(path);
 puts("GameFAQs discovery ingestion OK");
 return 0;
}
