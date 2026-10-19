#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ps2_max_archive.h"
#include "ps2_max_to_psu.h"
#include "psu.h"
#include <unistd.h>
int main(int argc, char **argv) {
    unsigned char bytes[4096], decoded[4096];
    ps2_max_entry entries[4];
    size_t n, count, size;
    void *data = NULL;
    char dir[] = "/tmp/maxpositiveXXXXXX", output[512];
    FILE *fp;
    assert(argc == 2);
    fp = fopen(argv[1], "rb"); assert(fp);
    size = fread(bytes, 1, sizeof(bytes), fp); assert(!ferror(fp));
    assert(feof(fp)); fclose(fp);
    assert(ps2_max_archive_list(bytes,size,decoded,sizeof(decoded),entries,4,&n,&count)==0);
    assert(count==1 && strcmp(entries[0].name,"icon.sys")==0);
    assert(entries[0].size==19 && memcmp(decoded+entries[0].data_offset,"VALID-MAX-FILE-DATA",19)==0);
    assert(mkdtemp(dir));
    snprintf(output,sizeof(output),"%s/test.psu",dir);
    assert(ps2_max_convert_to_psu(bytes,size,output)==0);
    assert(psu_read_file(output,"icon.sys",&data,&size)==0);
    assert(size==19 && memcmp(data,"VALID-MAX-FILE-DATA",19)==0);
    free(data);
    assert(ps2_max_convert_to_psu(bytes,size,output)!=0);
    unlink(output);rmdir(dir);
    puts("Positive MAX LZARI -> entries -> PSU -> read-back: PASS");
    return 0;
}
