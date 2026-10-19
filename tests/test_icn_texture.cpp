#include "icn_texture.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

static void append_u16(std::vector<uint8_t>& bytes,uint16_t value){
 bytes.push_back((uint8_t)value);bytes.push_back((uint8_t)(value>>8));
}
static void append_u32(std::vector<uint8_t>& bytes,uint32_t value){
 for(int i=0;i<4;i++)bytes.push_back((uint8_t)(value>>(i*8)));
}

int main(){
 /* Model the Pinball saves: FD/FE literal blocks plus a final repeated run. */
 std::vector<uint8_t> packed;
 append_u16(packed,0xFE1C); /* 484 literal pixels */
 for(unsigned i=0;i<484;i++)append_u16(packed,(uint16_t)(i&0x7FFF));
 append_u16(packed,0xFD7F); /* 641 literal pixels */
 for(unsigned i=0;i<641;i++)append_u16(packed,(uint16_t)((i+484)&0x7FFF));
 append_u16(packed,(uint16_t)(16384-484-641));
 append_u16(packed,0x7C00); /* repeated blue */

 std::vector<uint8_t> input;
 append_u32(input,(uint32_t)packed.size());
 input.insert(input.end(),packed.begin(),packed.end());
 std::vector<uint32_t> pixels(128u*128u,0);

 assert(ps2_icn_decode_rle(input.data(),input.size(),pixels.data()));
 assert(pixels.front()==0xFF000000u);
 assert(pixels[484]==(0xFF000000u|(((484u&31u)<<3)<<16)|((((484u>>5)&31u)<<3)<<8)|(((484u>>10)&31u)<<3)));
 assert(pixels[484+641-1]==(0xFF000000u|((1124u&31u)<<3<<16)|((((1124u>>5)&31u)<<3)<<8)|(((1124u>>10)&31u)<<3)));
 assert(pixels.back()==(0xFF000000u|(31u<<3)));

 /* Declared compressed lengths must stay inside the provided ICN data. */
 std::vector<uint8_t> truncated=input;
 truncated[0]=(uint8_t)(packed.size()+1u);
 assert(!ps2_icn_decode_rle(truncated.data(),truncated.size(),pixels.data()));
 std::puts("ICN RLE texture tests passed");
 return 0;
}
