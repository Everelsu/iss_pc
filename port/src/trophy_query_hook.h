// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
enum { ISS_TROPHY_QUERY_SITE=0x55b610 };
enum { ISS_TROPHY_RESET_SITE=0x1dcf30,ISS_TROPHY_RESET_CALLEE=0x1e7e70,ISS_TROPHY_PROGRESS_OBJECT=0xefad20 };
static const unsigned char iss_trophy_reset_signature[]={
 0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x89,0xfb,0xe8,0x31,0xaf,0x00,0x00,
 0xc5,0xf8,0x57,0xc0,0x48,0xb8,0x00,0x00,0x00,0x00,0x76,0x00,0x00,0x00,0xc5,0xf4,0x57};
static inline int iss_trophy_reset_valid(const unsigned char *image,size_t size){
 return image&&size>=ISS_TROPHY_RESET_SITE+sizeof(iss_trophy_reset_signature)&&
  !memcmp(image+ISS_TROPHY_RESET_SITE,iss_trophy_reset_signature,sizeof(iss_trophy_reset_signature));
}
// Original ten-byte prologue, absolute indirect call (with a return jump over
// its pointer), then absolute indirect jump to original +15. No register is
// borrowed and the original reset body runs exactly once.
static inline size_t iss_trophy_reset_trampoline(unsigned char *out,uintptr_t base){
 memcpy(out,iss_trophy_reset_signature,10);
 const unsigned char call[]={0xff,0x15,0x02,0,0,0,0xeb,0x08};memcpy(out+10,call,8);
 uintptr_t target=base+ISS_TROPHY_RESET_CALLEE;memcpy(out+18,&target,8);
 const unsigned char jump[]={0xff,0x25,0,0,0,0};memcpy(out+26,jump,6);
 target=base+ISS_TROPHY_RESET_SITE+15;memcpy(out+32,&target,8);return 40;
}
static const unsigned char iss_trophy_query_signature[]={
 0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,
 0x18,0x4c,0x8b,0x2d,0xd8,0x27,0x84,0x00,0x4c,0x8d,0x25,0xe1,0xdc,0xa1,0x07,0x41};
static inline int iss_trophy_query_valid(const unsigned char *image,size_t size,const char *title,const char *version){
 return image&&title&&version&&!strcmp(title,"CUSA00223")&&!strcmp(version,"01.07")&&
  size>=ISS_TROPHY_QUERY_SITE+sizeof(iss_trophy_query_signature)&&
  !memcmp(image+ISS_TROPHY_QUERY_SITE,iss_trophy_query_signature,sizeof(iss_trophy_query_signature));
}
