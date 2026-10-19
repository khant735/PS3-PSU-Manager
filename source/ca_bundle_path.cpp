#include "ca_bundle_path.h"

#include <stdio.h>
#include <string.h>

int ps3_ca_bundle_find(const char *const *candidates,size_t candidate_count,
                       char *selected,size_t selected_capacity){
 if(!candidates||!selected||selected_capacity==0)return -1;
 selected[0]=0;
 for(size_t i=0;i<candidate_count;i++){
  const char *path=candidates[i];
  if(!path||!*path||strlen(path)>=selected_capacity)continue;
  FILE *f=fopen(path,"rb");
  if(!f)continue;
  int readable=fseek(f,0,SEEK_END)==0&&ftell(f)>0;
  fclose(f);
  if(!readable)continue;
  snprintf(selected,selected_capacity,"%s",path);
  return 0;
 }
 return -1;
}
