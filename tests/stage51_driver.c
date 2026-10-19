#include "ps2_sharkport_header.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){int fails=0;for(int k=1;k<argc;k++){FILE*f=fopen(argv[k],"rb");if(!f)return 2;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);unsigned char*b=malloc(n);fread(b,1,n,f);fclose(f);ps2_sharkport_header h;int r=ps2_sharkport_read_header(b,n,&h);printf("%s: rc=%d chain=%d files=%u dir=%s\n",argv[k],r,r?0:h.record_chain_valid,r?0:h.validated_file_count,r?"":h.directory);if(r||!h.record_chain_valid)fails++;free(b);}return fails?1:0;}
