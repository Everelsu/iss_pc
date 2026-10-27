/* SPDX-License-Identifier: GPL-2.0-or-later
 * Experimental queued resize through the verified game-owned rebuild path.
 * No guest GPU descriptors or controller/progression fields are edited. */
#include "iss_native_live.h"
#include "iss_native_aspect.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include "iss_native_resolution.h"
static uintptr_t image_base;
static atomic_uint frames,targets;
static atomic_int rebuild_state;
static unsigned target_before,frame_before;
static uint32_t expected_width,expected_height;
static unsigned char previous_mode;
static atomic_int resize_enabled;
static atomic_uint resize_generation;
static atomic_uint purge_generation;
static atomic_int aspect_enabled;
static atomic_int aspect_camera_ready;
static atomic_int diagnostics_enabled;
static uint32_t purge_sizes[4];
static _Atomic(const char*) resize_reason="Live resize unavailable";
static atomic_flag request_lock=ATOMIC_FLAG_INIT;
static _Atomic(uint64_t) *aspect_pair;
static IssNativePlan pending_plan,before_plan;
static uint32_t lighting_capacity;
static uint32_t allocation_width,allocation_height;
static uint64_t pixel_capacity;
static int changing_dimensions,rolling_back;
static void lock_request(void){while(atomic_flag_test_and_set_explicit(&request_lock,memory_order_acquire)){} }
static void unlock_request(void){atomic_flag_clear_explicit(&request_lock,memory_order_release);}
void runtime_native_live_attach(uintptr_t base){
 image_base=base;const char* trace=getenv("ISS_NATIVE_LIVE_TRACE");
 atomic_store(&diagnostics_enabled,trace&&!strcmp(trace,"1"));
}
static uint32_t read32(const void *p){uint32_t value;memcpy(&value,p,4);return value;}
void __attribute__((ms_abi)) runtime_native_frame_observer(const void *object,int frame){
 if(!image_base||(uintptr_t)object!=image_base+0xf1bcd0u)return;
 unsigned count=atomic_fetch_add(&frames,1)+1;
 if(!atomic_load(&diagnostics_enabled)||(count!=1&&count!=300&&count%3600))return;
 const unsigned char *p=object;
 fprintf(stderr,"NATIVE_LIVE_FRAME calls=%u index=%d surface=%ux%u scene=%ux%u reconfigured=%u mode=%u targets=%u\n",count,frame,
  read32(p),read32(p+4),read32(p+12),read32(p+16),p[0xdf0f04],p[0xdf0f05],atomic_load(&targets));
}
void __attribute__((ms_abi)) runtime_native_target_observer(const void *object,int unused){
 (void)unused;if(!image_base||(uintptr_t)object!=image_base+0xe682b0u)return;
 unsigned count=atomic_fetch_add(&targets,1)+1;
 if(atomic_load(&diagnostics_enabled)&&count<=4)fprintf(stderr,"NATIVE_LIVE_TARGETS rebuild=%u after_frame_calls=%u\n",count,atomic_load(&frames));
}
int runtime_native_live_rebuild_state(void){return atomic_load(&rebuild_state);}
int runtime_native_live_enabled(void){return atomic_load(&resize_enabled);}
int runtime_native_live_capacity(uint32_t *width,uint32_t *height,uint32_t *lights){
 if(!runtime_native_live_enabled()||!width||!height||!lights)return 0;
 *width=allocation_width;*height=allocation_height;*lights=lighting_capacity;return 1;
}
uint32_t runtime_native_live_generation(void){return atomic_load(&resize_generation);}
void runtime_native_live_aspect_enable(void){atomic_store(&aspect_enabled,1);}
void __attribute__((ms_abi)) runtime_native_live_aspect_camera(void *object,int unused){
 (void)unused;
 if(!atomic_load(&aspect_enabled)||!runtime_native_live_enabled()||!image_base||!object)return;
 atomic_store(&aspect_camera_ready,0);
 uintptr_t main_camera;memcpy(&main_camera,(void*)(image_base+0x7f2d2b8u),sizeof(main_camera));
 if((uintptr_t)object!=main_camera)return;
 uintptr_t vtable;memcpy(&vtable,object,sizeof(vtable));
 /* Only the independently verified perspective-camera virtual methods.
  * Orthographic, minimap, shadow and unknown derived cameras stay untouched. */
 if(vtable<image_base||vtable-image_base>0x7f00000u-0x130u)return;
 uintptr_t projection,frustum;memcpy(&projection,(void*)(vtable+0x120),sizeof(projection));
 memcpy(&frustum,(void*)(vtable+0x128),sizeof(frustum));
 if(projection!=image_base+0x581c90u||frustum!=image_base+0x5821b0u)return;
 unsigned char *cam=object;float old_aspect,tan_x,tan_y,aspect,next_x;
 memcpy(&old_aspect,cam+0x59c,4);memcpy(&tan_x,cam+0x5a0,4);memcpy(&tan_y,cam+0x5a4,4);
 IssNativePlan plan=*runtime_native_plan();
 if(!iss_native_aspect_tangent(old_aspect,tan_x,tan_y,plan.width,plan.height,&aspect,&next_x))return;
 if(!atomic_exchange(&aspect_camera_ready,1)){
  static unsigned reports;if(reports++<2)fputs("Native live aspect: active perspective camera layout verified\n",stderr);
 }
 if(fabsf(aspect-old_aspect)<0.0001f)return;
 /* Keep tan(vertical FOV / 2), position and input data unchanged. The game's
  * setter rebuilds projection and frustum through its original virtuals. */
 memcpy(cam+0x5a0,&next_x,4);
 typedef void (__attribute__((sysv_abi)) *AspectSetter)(void*,float);
 ((AspectSetter)(image_base+0x581b70u))(object,aspect);
 fprintf(stderr,"NATIVE_LIVE_ASPECT camera refreshed %.6g -> %.6g; tan_y=%.6g\n",(double)old_aspect,(double)aspect,(double)tan_y);
}
uint32_t runtime_native_live_purge_plan(uint32_t sizes[4]){
 if(!sizes||!atomic_load(&purge_generation))return 0;
 lock_request();uint32_t generation=atomic_load(&purge_generation);
 memcpy(sizes,purge_sizes,sizeof(purge_sizes));unlock_request();return generation;
}
const char *runtime_native_live_reason(void){return atomic_load(&resize_reason);}
void runtime_native_live_enable(uint32_t *cells,const IssNativePlan *startup){
 if(!cells||((uintptr_t)cells&7)||!startup||startup->ui_16_9!=1||startup->ui_width!=1920||startup->ui_height!=1080)return;
 aspect_pair=(_Atomic(uint64_t)*)cells;lighting_capacity=startup->light_entries;pixel_capacity=(uint64_t)startup->width*startup->height;
 allocation_width=startup->width;allocation_height=startup->height;
 atomic_store(aspect_pair,(uint64_t)startup->width|((uint64_t)startup->height<<32));
 atomic_store(&resize_generation,1);
 atomic_store(&resize_reason,"Live resize ready within this launch's allocation budget");atomic_store(&resize_enabled,1);
}
int runtime_native_live_request_config(IssNativeConfig config){
 if(!runtime_native_live_enabled())return 0;
 IssNativePlan next;
 if(!iss_native_plan(config,&next)||next.ui_16_9!=1){atomic_store(&resize_reason,"This mode requires restarting (original/alternate UI canvas)");return 0;}
 IssNativePlan current=*runtime_native_plan();
 if((!atomic_load(&aspect_enabled)||!atomic_load(&aspect_camera_ready))&&fabs((double)next.width/next.height-(double)current.width/current.height)>0.003){
  atomic_store(&resize_reason,"A different aspect ratio requires restarting (camera refresh unavailable)");return 0;
 }
 if(next.width>allocation_width||next.height>allocation_height||next.light_entries>lighting_capacity||
    (uint64_t)next.width*next.height>pixel_capacity){atomic_store(&resize_reason,"Larger render allocation requires restarting this candidate");return 0;}
 lock_request();int state=atomic_load(&rebuild_state);
 if(state==1||state==2){unlock_request();atomic_store(&resize_reason,"Resize in progress; retry after it finishes");return 0;}
 pending_plan=next;before_plan=*runtime_native_plan();changing_dimensions=1;rolling_back=0;
 atomic_store(&resize_reason,"Resize queued for the render-thread safe point");atomic_store(&rebuild_state,1);unlock_request();return 1;
}
int runtime_native_live_rebuild_request(void){
 const char *enabled=getenv("ISS_NATIVE_LIVE_REBUILD_TEST");
 if(!image_base||!enabled||strcmp(enabled,"1")||runtime_native_status()!=1)return 0;
 lock_request();int state=atomic_load(&rebuild_state);
 if(state==1||state==2){unlock_request();return 0;}
 changing_dimensions=0;rolling_back=0;atomic_store(&rebuild_state,1);unlock_request();return 1;
}
void __attribute__((ms_abi)) runtime_native_dimension_resize(void *object,uint64_t *saved){
 if(!runtime_native_live_enabled()||(uintptr_t)object!=image_base+0xf1bcd0u||!saved)return;
 const IssNativePlan plan=changing_dimensions&&atomic_load(&rebuild_state)==2?pending_plan:*runtime_native_plan();
 iss_native_live_dimension_registers(saved,&plan);
}
void __attribute__((ms_abi)) runtime_native_safe_resize(void *object){
 if(!image_base||(uintptr_t)object!=image_base+0xf1bcd0u)return;
 unsigned char *p=object;int state=atomic_load(&rebuild_state);
 if(state==2){
  if(atomic_load(&targets)>target_before&&p[0xdf0f08]<=1&&read32(p)==expected_width&&read32(p+4)==expected_height&&read32(p+12)==expected_width&&read32(p+16)==expected_height){
   if(changing_dimensions){
    atomic_store(aspect_pair,(uint64_t)pending_plan.width|((uint64_t)pending_plan.height<<32));
    runtime_native_publish(1,"Experimental live native rendering active",&pending_plan);
    if(!rolling_back&&(before_plan.width!=pending_plan.width||before_plan.height!=pending_plan.height)){
     lock_request();purge_sizes[0]=before_plan.width;purge_sizes[1]=before_plan.height;
     purge_sizes[2]=pending_plan.width;purge_sizes[3]=pending_plan.height;
     atomic_fetch_add(&purge_generation,1);unlock_request();
    }
    atomic_store(&resize_reason,rolling_back?"Previous resolution restored; change was not confirmed":"Live resize applied and target recreation verified");
    fprintf(stderr,"NATIVE_LIVE_RESIZE verified: %ux%u at %d%%; rollback=%d\n",pending_plan.width,pending_plan.height,pending_plan.percent,rolling_back);
   }else fputs("NATIVE_LIVE_REBUILD verified: target recreation observed; scene dimensions unchanged\n",stderr);
   atomic_store(&rebuild_state,rolling_back?-1:3);
  }else if(atomic_load(&frames)-frame_before>3){
   if(p[0xdf0f08]==2)p[0xdf0f08]=previous_mode;
   if(changing_dimensions&&!rolling_back){pending_plan=before_plan;rolling_back=1;atomic_store(&resize_reason,"Rebuild unconfirmed; restoring previous resolution");atomic_store(&rebuild_state,1);return;}
   atomic_store(&resize_reason,"Rebuild could not be verified; restart recommended");
   atomic_store(&rebuild_state,-1);fputs("NATIVE_LIVE_REBUILD refused: target recreation not confirmed\n",stderr);
  }
  return;
 }
 if(state!=1)return;
 expected_width=read32(p);expected_height=read32(p+4);
 if(p[0xdf0f08]>1||!expected_width||!expected_height||(uint64_t)expected_width*expected_height>3840u*2160u||read32(p+12)!=expected_width||read32(p+16)!=expected_height){atomic_store(&rebuild_state,-1);return;}
 target_before=atomic_load(&targets);frame_before=atomic_load(&frames);previous_mode=p[0xdf0f08];
 if(changing_dimensions){expected_width=pending_plan.width;expected_height=pending_plan.height;}
 /* This hook runs after the original game's command-buffer waits. Invalidating
  * only its cached mode byte makes the original comparison enter its normal
  * rebuild branch. The game immediately replaces that byte with its mode.
  * The original dimensions and original target recreation code are retained. */
 if(changing_dimensions)atomic_fetch_add(&resize_generation,1);
 p[0xdf0f08]=2;atomic_store(&rebuild_state,2);
 fprintf(stderr,"NATIVE_LIVE_REBUILD requested: same %ux%u; game-owned rebuild path\n",expected_width,expected_height);
}
