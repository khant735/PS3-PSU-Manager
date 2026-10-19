#include "ps2_sharkport_header.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){int i;for(i=1;i<argc;i++){FILE*f=fopen(argv[i],"rb");long n;unsigned char*b;ps2_sharkport_header h;if(!f)return 2;fseek(f,0,SEEK_END);n=ftell(f);rewind(f);b=malloc(n);if(!b)return 2;if(fread(b,1,n,f)!=(size_t)n)return 2;fclose(f);if(ps2_sharkport_read_header(b,n,&h)){fprintf(stderr,"failed %s\n",argv[i]);return 1;}printf("%s | %s | %s | offset %zu\n",argv[i],h.title,h.description,h.payload_offset);free(b);}return 0;}
