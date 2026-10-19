#include "ps2_save_probe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    const char *path="ps2_probe_test.tmp";
    unsigned char data[128]={0};
    ps2_save_probe_result r;
    FILE *f;
    assert(ps2_save_probe_file(NULL,&r)==-1);
    assert(ps2_save_probe_file("file_that_does_not_exist.abc",&r)==-2);
    f=fopen(path,"wb"); assert(f);
    memcpy(data,"Ps2PowerSave",12); assert(fwrite(data,1,sizeof(data),f)==sizeof(data)); fclose(f);
    assert(ps2_save_probe_file(path,&r)==0);
    assert(r.format==PS2_SAVE_MAX && r.file_size==128 && r.signature_valid && r.payload_present && !r.supported_decoder);
    remove(path);
    f=fopen(path,"wb"); assert(f);
    memcpy(data,"CFU\0",4); assert(fwrite(data,1,4,f)==4); fclose(f);
    assert(ps2_save_probe_file(path,&r)==0);
    assert(r.format==PS2_SAVE_CBS && !r.payload_present && !r.supported_decoder);
    remove(path);
    return 0;
}
