/* SPDX-License-Identifier: GPL-2.0-or-later */
#undef NDEBUG
#include "../src/iss_native_aspect.h"
#include <cassert>
#include <vector>
#include <cstdio>
#include "../src/iss_karma.h"
#ifdef _WIN32
#include <windows.h>
static const void* observed;
static int observed_index;
static void __attribute__((ms_abi)) observe(const void* object,int index,uint64_t){observed=object;observed_index=index;}
#endif
int main(){
 std::vector<unsigned char> image(0x600000);
 struct Range{size_t at;const unsigned char* bytes;size_t n;};
 const Range ranges[]={
  {ISS_NATIVE_ASPECT_SITE,iss_native_aspect_site,sizeof(iss_native_aspect_site)},
  {0x472909,iss_native_main_load,sizeof(iss_native_main_load)},
  {0x581b70,iss_native_aspect_setter,sizeof(iss_native_aspect_setter)},
  {0x581c90,iss_native_aspect_projection,sizeof(iss_native_aspect_projection)},
  {0x5821b0,iss_native_aspect_frustum,sizeof(iss_native_aspect_frustum)}};
 for(auto r:ranges)memcpy(image.data()+r.at,r.bytes,r.n);
 assert(iss_native_aspect_signature(image.data(),image.size()));
 for(auto r:ranges)for(size_t i=0;i<r.n;i++){
  image[r.at+i]^=1;assert(!iss_native_aspect_signature(image.data(),image.size()));image[r.at+i]^=1;
 }
 assert(!iss_native_aspect_signature(image.data(),0x5821b0+sizeof(iss_native_aspect_frustum)-1));
 float aspect=3440.f/1440.f,y=0.577350269f,x=y*aspect,next,next_x;
 assert(iss_native_aspect_tangent(aspect,x,y,2560,1440,&next,&next_x));
 assert(fabsf(next-16.f/9.f)<0.00001f&&fabsf(next_x/y-next)<0.00001f);
 assert(iss_native_aspect_tangent(next,next_x,y,3440,1440,&next,&next_x));
 assert(fabsf(next_x-x)<0.00001f);
 assert(iss_native_aspect_tangent(aspect,x,y,1720,720,&next,&next_x));
 assert(fabsf(next-aspect)<0.00001f);
 assert(!iss_native_aspect_tangent(aspect,x*2,y,2560,1440,&next,&next_x));
 assert(!iss_native_aspect_tangent(NAN,x,y,2560,1440,&next,&next_x));
 assert(!iss_native_aspect_tangent(aspect,x,0,2560,1440,&next,&next_x));
 assert(!iss_native_aspect_tangent(aspect,x,y,2560,0,&next,&next_x));
 assert(!iss_native_aspect_tangent(aspect,x,y,1,1440,&next,&next_x));
#ifdef _WIN32
 // Execute the exact mid-function instructions after the generated ABI bridge.
 auto* code=(unsigned char*)VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(code);
 unsigned n=iss_karma_bridge_ex(code,(uintptr_t)observe,iss_native_aspect_site,(uintptr_t)code+2048,ISS_NATIVE_ASPECT_STOLEN,1);assert(n<2048);
 code[2048]=0xc3;
 const unsigned char wrapper[]={0x53,0xbe,0x34,0x12,0,0,0x48,0xb8};
 memcpy(code+3000,wrapper,sizeof(wrapper));uintptr_t bridge=(uintptr_t)code;memcpy(code+3000+sizeof(wrapper),&bridge,8);
 const unsigned char tail[]={0xff,0xd0,0x5b,0xc3};memcpy(code+3000+sizeof(wrapper)+8,tail,sizeof(tail));
 DWORD old;assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),code,4096);
 unsigned char camera[0x700]{};
 using Fn=uint64_t(__attribute__((sysv_abi)) *)(void*);
 assert(((Fn)(code+3000))(camera)==0x12);
 assert(observed==camera && observed_index==0x1234);
 VirtualFree(code,0,MEM_RELEASE);
#endif
 puts("Native aspect signatures, every-byte mismatch, truncation, Hor+ round trip, same-shape scaling and invalid camera rejection passed");
}
