/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "iss_native_resolution.h"
/* Enabled by default; explicit zero or malformed overrides fail closed. */
static inline int iss_native_live_option(const char *value){return !value||!strcmp(value,"1");}
enum { ISS_NATIVE_FRAME_SITE=0x1f0be0,ISS_NATIVE_FRAME_STOLEN=20,
       ISS_NATIVE_TARGET_SITE=0x18e210,ISS_NATIVE_TARGET_STOLEN=17,
       ISS_NATIVE_SAFE_SITE=0x1f0c67,ISS_NATIVE_SAFE_STOLEN=15,
       ISS_NATIVE_DIM_SITE=0x1f0d6e,ISS_NATIVE_DIM_STOLEN=16 };
static const unsigned char iss_native_frame_signature[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x88,0,0,0};
static const unsigned char iss_native_target_signature[]={0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x89,0xfb,0x48,0x8d,0xbb,0xdc,0x1e,1,0};
static const unsigned char iss_native_safe_signature[]={0x4d,0x89,0xa5,0xa0,0xdc,1,0,0x41,0xc6,0x85,4,0x0f,0xdf,0,0};
static const unsigned char iss_native_dim_signature[]={0x41,0x89,0x5d,0,0x41,0x89,0x4d,4,0x41,0x89,0x45,0x0c,0x41,0x89,0x55,0x10};
static inline int iss_native_live_signature(const unsigned char *image,size_t size,const char *title,const char *version){
 return image&&title&&version&&!strcmp(title,"CUSA00223")&&!strcmp(version,"01.07")&&
  size>=ISS_NATIVE_FRAME_SITE+sizeof(iss_native_frame_signature)&&size>=ISS_NATIVE_TARGET_SITE+sizeof(iss_native_target_signature)&&
  size>=ISS_NATIVE_SAFE_SITE+sizeof(iss_native_safe_signature)&&
  size>=ISS_NATIVE_DIM_SITE+sizeof(iss_native_dim_signature)&&
  !memcmp(image+ISS_NATIVE_FRAME_SITE,iss_native_frame_signature,sizeof(iss_native_frame_signature))&&
  !memcmp(image+ISS_NATIVE_TARGET_SITE,iss_native_target_signature,sizeof(iss_native_target_signature))&&
  !memcmp(image+ISS_NATIVE_SAFE_SITE,iss_native_safe_signature,sizeof(iss_native_safe_signature))&&
  !memcmp(image+ISS_NATIVE_DIM_SITE,iss_native_dim_signature,sizeof(iss_native_dim_signature));
}
/* Saved GPR slots in the bridge: R15,R14,R13,R12,R11,R10,R9,R8,
 * RDI,RSI,RBP,RBX,RDX,RCX,RAX,RFLAGS. Only dimension-producing GPRs change. */
static inline void iss_native_live_dimension_registers(uint64_t *saved,const IssNativePlan *p){
 saved[11]=p->width;saved[13]=p->height;saved[14]=p->width;saved[12]=p->height;
 saved[9]=(saved[6]&1)?p->width:0;saved[8]=(saved[6]&1)?p->height:0;
}
static inline int iss_native_live_projection_redirect(unsigned char *image,size_t size,const IssNativePlan *plan,uintptr_t cells){
 if(!image||!plan||!cells||size<0x58030c+sizeof(iss_native_projection))return 0;
 IssNativeSite sites[ISS_NATIVE_SITE_COUNT];iss_native_changes(plan,sites);
 if(memcmp(image+sites[16].offset,sites[16].after,7)||memcmp(image+sites[17].offset,sites[17].after,7)||
    memcmp(image+0x58030c+7,iss_native_projection+7,sizeof(iss_native_projection)-7))return 0;
 int32_t displacements[2];
 for(unsigned i=0;i<2;i++){
  intptr_t distance=(intptr_t)(cells+i*4)-(intptr_t)(image+sites[16+i].offset+7);
  if(distance<INT32_MIN||distance>INT32_MAX)return 0;
  displacements[i]=(int32_t)distance;
 }
 for(unsigned i=0;i<2;i++)memcpy(image+sites[16+i].offset+3,&displacements[i],4);
 return 1;
}
#ifdef __cplusplus
extern "C" {
#endif
void runtime_native_live_attach(uintptr_t base);
void __attribute__((ms_abi)) runtime_native_frame_observer(const void *object,int frame);
void __attribute__((ms_abi)) runtime_native_target_observer(const void *object,int unused);
void __attribute__((ms_abi)) runtime_native_safe_resize(void *object);
int runtime_native_live_rebuild_request(void);
int runtime_native_live_rebuild_state(void);
void runtime_native_live_enable(uint32_t *aspect_cells,const IssNativePlan *startup);
int runtime_native_live_enabled(void);
int runtime_native_live_capacity(uint32_t *width,uint32_t *height,uint32_t *lights);
/* Diagnostic resize serial; zero until enabled. No GPU state is touched here. */
uint32_t runtime_native_live_generation(void);
/* CP requests a coherent confirmed old/new size pair for deferred retirement. */
uint32_t runtime_native_live_purge_plan(uint32_t sizes[4]);
void runtime_native_live_aspect_enable(void);
void __attribute__((ms_abi)) runtime_native_live_aspect_camera(void *object,int unused);
int runtime_native_live_request_config(IssNativeConfig config);
const char *runtime_native_live_reason(void);
void __attribute__((ms_abi)) runtime_native_dimension_resize(void *object,uint64_t *saved);
#ifdef __cplusplus
}
#endif
