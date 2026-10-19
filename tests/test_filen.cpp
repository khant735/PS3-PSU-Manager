#include "filen.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
static std::vector<unsigned char> read(const char *p){FILE*f=fopen(p,"rb");assert(f);std::vector<unsigned char>b;int c;while((c=fgetc(f))!=EOF)b.push_back(c);fclose(f);return b;}
int main(int argc,char**argv){
 assert(argc==5);char uuid[37],key[257];
 assert(filen_parse_link("https://app.filen.io/#/d/4c614ee3-fb09-4765-8dc2-0656508ad023#741JqXO5MJZ6K7a9dLTTf1Rp6v1Qkh3s",uuid,key));
 assert(!filen_parse_link("https://app.filen.io.evil/#/d/4c614ee3-fb09-4765-8dc2-0656508ad023#abcdefghijkl",uuid,key));
 std::vector<unsigned char>b=read(argv[2]),out;std::string err;
 int version=atoi(argv[1]);bool meta=!strcmp(argv[3],"meta");
 bool ok=meta?filen_decrypt_metadata(std::string(b.begin(),b.end()),argv[4],out,err):filen_decrypt_chunk(b,argv[4],version,out,err);
 if(!ok){fprintf(stderr,"%s\n",err.c_str());return 2;}
 if(!out.empty())fwrite(&out[0],1,out.size(),stdout);
 return 0;
}
