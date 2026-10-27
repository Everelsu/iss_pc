#include "../src/iss_native_resolution.h"
#include <cassert>
#include <vector>
#include <cstdio>
static unsigned read32(const unsigned char*p){unsigned n;memcpy(&n,p,4);return n;}
int main(){
 assert(iss_native_exposure_entries(1920,1080)==625);
 assert(iss_native_exposure_entries(2560,1440)==625);
 assert(iss_native_exposure_entries(3440,1440)==625);
 assert(iss_native_exposure_entries(3840,2160)==912);
 assert(iss_native_exposure_entries(5120,1440)==800);
 assert(!iss_native_exposure_entries(0,1440));assert(!iss_native_exposure_entries(3840,2161));
 for(int preset=1;preset<ISS_NATIVE_PRESETS;preset++)for(int scale=25;scale<=200;scale++){
  IssNativePlan p;if(!iss_native_plan({preset,scale,1},&p))continue;
  unsigned need=((p.width+127)/128)*((p.height+71)/72);
  assert(iss_native_exposure_entries(p.width,p.height)>=need);
 }
 std::vector<unsigned char> original(0x600000,0xcc);
 for(const auto&s:iss_native_sites)memcpy(original.data()+s.offset,s.before,s.length);
 memcpy(original.data()+0x58030c,iss_native_projection,sizeof(iss_native_projection));
 memcpy(original.data()+0x5802e6,iss_native_aspect_width,sizeof(iss_native_aspect_width));
 memcpy(original.data()+0x171310,iss_exposure_allocator,sizeof(iss_exposure_allocator));
 for(const auto&s:iss_exposure_sites)memcpy(original.data()+s.offset,s.before,5);
 assert(iss_native_exposure_guard(original.data(),original.size()));
 auto check=[&](size_t at){auto b=original;b[at]^=1;auto before=b;assert(!iss_native_apply(b.data(),b.size(),"CUSA00223","01.07",{4,100,1}));assert(b==before);};
 for(unsigned i=0;i<sizeof(iss_exposure_allocator);i++)check(0x171310+i);
 for(const auto&s:iss_exposure_sites){for(unsigned i=0;i<5;i++)check(s.offset+i);assert(!iss_native_exposure_guard(original.data(),s.offset+4));}
 for(const char* version:{"01.00","01.08"}){auto b=original;assert(!iss_native_apply(b.data(),b.size(),"CUSA00223",version,{4,100,1}));assert(b==original);}
 auto b=original;assert(!iss_native_apply(b.data(),b.size(),"CUSA00004","01.07",{4,100,1}));assert(b==original);
 b=original;assert(iss_native_apply(b.data(),b.size(),"CUSA00223","01.07",{4,100,1}));
 auto expected=original;IssNativePlan p;assert(iss_native_plan({4,100,1},&p));IssNativeSite changes[ISS_NATIVE_SITE_COUNT];iss_native_changes(&p,changes);
 for(const auto&s:changes)memcpy(expected.data()+s.offset,s.after,s.length);
 for(unsigned i=0;i<ISS_EXPOSURE_SITE_COUNT;i++){unsigned value=(i&1)?912:3648;memcpy(expected.data()+iss_exposure_sites[i].offset+1,&value,4);assert(read32(b.data()+iss_exposure_sites[i].offset+1)==value);}
 assert(b==expected);
 b=original;assert(iss_native_apply(b.data(),b.size(),"CUSA00223","01.07",{3,100,1}));for(const auto&s:iss_exposure_sites)assert(!memcmp(b.data()+s.offset,s.before,5));
 puts("Exposure capacity: grid coverage, both allocations/descriptors, every-byte guards, atomic fallback and unchanged lower resolutions passed.");
}

