#include "ca_bundle_path.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

int main(){
 const char *dir="/tmp/ps3-ca-bundle-path-test";
 mkdir(dir,0700);
 const char *missing="/tmp/ps3-ca-bundle-path-test/missing.pem";
 const char *present="/tmp/ps3-ca-bundle-path-test/readable.pem";
 FILE *f=fopen(present,"wb");assert(f);assert(fwrite("certificate bundle\n",1,19,f)==19);assert(fclose(f)==0);
 const char *candidates[]={missing,present};
 char selected[256]={0};
 assert(ps3_ca_bundle_find(candidates,2,selected,sizeof(selected))==0);
 assert(std::strcmp(selected,present)==0);
 char too_small[4]={0};
 assert(ps3_ca_bundle_find(candidates,2,too_small,sizeof(too_small))!=0);
 const char *none[]={missing};
 assert(ps3_ca_bundle_find(none,1,selected,sizeof(selected))!=0);
 unlink(present);rmdir(dir);
 std::puts("CA bundle path tests passed");
 return 0;
}
