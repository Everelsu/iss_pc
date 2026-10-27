/* SPDX-License-Identifier: GPL-2.0-or-later */
#undef NDEBUG
#include "../src/iss_native_resolution.h"
#include <cassert>
#include <vector>
#include <cstdio>
int main(){
 IssNativePlan plan;assert(iss_native_plan({2,100,0,1},&plan));
 IssNativeSite sites[ISS_NATIVE_SITE_COUNT];iss_native_changes(&plan,sites);
 std::vector<unsigned char> image(0x600000,0);
 for(auto& s:sites)memcpy(image.data()+s.offset,s.after,s.length);
 auto original=image;
 assert(!iss_native_raise_light_capacity(image.data(),image.size(),&plan,0x310000));assert(image==original);
 assert(!iss_native_raise_light_capacity(image.data(),image.size(),&plan,0x400001));assert(image==original);
 assert(!iss_native_raise_light_capacity(image.data(),image.size(),&plan,0x3fffff));assert(image==original);
 for(unsigned site=14;site<16;site++)for(unsigned byte=0;byte<sites[site].length;byte++){
  image[sites[site].offset+byte]^=1;auto mismatched=image;
  assert(!iss_native_raise_light_capacity(image.data(),image.size(),&plan,0x400000));assert(image==mismatched);
  image[sites[site].offset+byte]^=1;
 }
 assert(!iss_native_raise_light_capacity(image.data(),0x16be22,&plan,0x400000));assert(image==original);
 assert(iss_native_raise_light_capacity(image.data(),image.size(),&plan,0x400000));
 iss_native_write32(original.data()+sites[14].offset+3,0x400000);
 iss_native_write32(original.data()+sites[15].offset+2,0x400000);
 assert(image==original);assert(plan.width==2560&&plan.height==1440&&plan.ui_width==1920);
 puts("Capacity-only lighting control: whole-instruction guards, every-byte corruption, bounds and unchanged scene passed");
}
