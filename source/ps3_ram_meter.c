#include "ps3_ram_meter.h"
#include <ppu-lv2.h>
#include <stdint.h>
typedef struct {
 uint32_t total_user_memory;
 uint32_t available_user_memory;
} ps3_user_memory_info;
int ps3_ram_get_user_pool(unsigned int *total_bytes,unsigned int *available_bytes){
 ps3_user_memory_info info={0,0};
 if(!total_bytes||!available_bytes)return -1;
 lv2syscall1(352,(uint64_t)&info);
 s32 rc=(s32)p1;
 if(rc!=0||info.total_user_memory==0||info.available_user_memory>info.total_user_memory)return -1;
 *total_bytes=info.total_user_memory;
 *available_bytes=info.available_user_memory;
 return 0;
}
