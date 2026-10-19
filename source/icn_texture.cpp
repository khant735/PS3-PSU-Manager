#include "icn_texture.h"

static uint16_t read_u16le(const uint8_t *p){
 return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t read_u32le(const uint8_t *p){
 return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t decode_bgr555(uint16_t value){
 uint32_t r=(value&0x1Fu)<<3;
 uint32_t g=((value>>5)&0x1Fu)<<3;
 uint32_t b=((value>>10)&0x1Fu)<<3;
 return 0xFF000000u|(r<<16)|(g<<8)|b;
}

int ps2_icn_decode_rle(const uint8_t *src,size_t src_size,uint32_t *texture){
 if(!src||!texture||src_size<4u)return 0;
 uint32_t packed_size=read_u32le(src);
 if(packed_size==0u || (size_t)packed_size>src_size-4u)return 0;
 const uint8_t *cur=src+4,*limit=cur+packed_size;
 size_t out=0;
 while(out<128u*128u && (size_t)(limit-cur)>=2u){
  uint16_t code=read_u16le(cur);cur+=2;
  size_t remaining=128u*128u-out;
  size_t repeat_count=(size_t)code;
  /* ICN producers use several high-byte ranges for literal blocks.  Rather
     than hard-code only FFxx, distinguish the two packet forms by which one
     can fit in the remaining 128x128 output.  Codes FE1C and FD7F in the
     supplied Pinball icons are literal blocks of 484 and 641 pixels. */
  if(repeat_count>0u && repeat_count<=remaining){
   if((size_t)(limit-cur)<2u)return 0;
   uint32_t color=decode_bgr555(read_u16le(cur));cur+=2;
   for(size_t k=0;k<repeat_count;k++)texture[out++]=color;
  }else{
   size_t literal_count=(size_t)(0x10000u-(uint32_t)code);
   if(literal_count==0u||literal_count>remaining||(size_t)(limit-cur)<literal_count*2u)return 0;
   for(size_t k=0;k<literal_count;k++,cur+=2)texture[out++]=decode_bgr555(read_u16le(cur));
  }
 }
 return out==128u*128u && cur==limit;
}
