#include "iss_power_wheel.h"
#include "power_wheel_transaction.h"
#include "power_wheel_signatures.h"
#include "power_wheel_gameplay.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>

static unsigned char *pw_image;
static atomic_int pw_supported,pw_ready,pw_active,pw_mask,pw_error,pw_requested;
static atomic_int pw_visual_open,pw_visual_choice;
static atomic_uint pw_generation;
static unsigned pw_seen_generation;
static uintptr_t pw_hero;
static PwBank pw_bank;
static _Thread_local int pw_inside;
static atomic_flag pw_working=ATOMIC_FLAG_INIT;
extern int tlggpu_overlay_captures_input(void);
static uint32_t pw_u32(const void *p){uint32_t n;memcpy(&n,p,4);return n;}
static uintptr_t pw_pointer(const void *p){uintptr_t n;memcpy(&n,p,sizeof(n));return n;}
static int pw_guest(uintptr_t p){return p>=0x1000000000ull&&p<0x10000000000ull;}
static int pw_read_game(void *context,PwSample *out){
    (void)context;
    uintptr_t hero=pw_pointer(pw_image+0x1d0cfe0);
    if(!pw_guest(hero)||hero!=pw_hero)return 0;
    unsigned char *a=pw_image+0xefacf8,*h=(unsigned char*)hero;
    out->native=(int)pw_u32(a+0xe4);out->missiles=(int)pw_u32(a+0xdc);
    out->capacity=(int)pw_u32(h+0x1ee4c);
    memcpy(&out->energy,h+0x1a1e8,4);memcpy(&out->maximum,h+0x1a1f0,4);
    for(int i=0;i<PW_COUNT;i++)out->unlocked[i]=pw_u32(a+0xe8+pw_family_id[i]*4);
    return pw_sample_layout_valid(out);
}
typedef void (__attribute__((sysv_abi)) *PwChange)(void *,int);
typedef void (__attribute__((sysv_abi)) *PwAmmo)(void *,int);
static int pw_change_game(void *context,int native){
    (void)context;
    if(pw_pointer(pw_image+0x1d0cfe0)!=pw_hero)return 0;
    ((PwChange)(pw_image+0x2143d0))((void*)pw_hero,native);
    return 1;
}
static int pw_write_game(void *context,float energy,int missiles){
    PwSample current;
    if(!pw_read_game(context,&current)||!isfinite(energy)||energy<0||energy>current.maximum||
       missiles<0||missiles>current.capacity)return 0;
    memcpy((void*)(pw_hero+0x1a1e8),&energy,4);
    ((PwAmmo)(pw_image+0x1e35a0))(pw_image+0xefacf8,missiles);
    return 1;
}
void runtime_power_wheel_attach(unsigned char *image,size_t size){
    const char *opt=getenv("ISS_POWER_WHEEL_TEST");
    if(!opt||strcmp(opt,"1"))return;
    if(!pw_signatures(image,size)||!pw_pause_signature(image,size)){
        puts("Power wheel: disabled (game-function or pause-state signatures differ)");return;
    }
    pw_image=image;pw_reset(&pw_bank,0);pw_hero=0;
    atomic_store(&pw_requested,0);atomic_store(&pw_ready,0);atomic_store(&pw_error,0);
    atomic_store(&pw_active,-1);atomic_store(&pw_mask,0);atomic_store(&pw_supported,1);
    puts("Power wheel: experimental game-thread switching enabled; current resources carry across every switch");
}
void runtime_power_wheel_load_begin(void){
    atomic_store(&pw_visual_open,0);
    atomic_store(&pw_ready,0);atomic_store(&pw_requested,0);atomic_store(&pw_mask,0);
    atomic_fetch_add(&pw_generation,1);
}
void runtime_power_wheel_request(int index){
    if(index<0){atomic_store(&pw_requested,0);atomic_store(&pw_visual_open,0);return;}
    if(index>=0&&index<PW_COUNT&&runtime_power_wheel_gameplay()&&atomic_load(&pw_ready)&&!atomic_load(&pw_error))
        atomic_store(&pw_requested,index+1);
}
int runtime_power_wheel_gameplay(void){
    /* The guest's writable segments are not mapped yet during renderer startup.
     * Only the camera-thread's validated hero/resource sample publishes ready. */
    int verified=atomic_load(&pw_supported)&&atomic_load(&pw_ready);
    uint32_t paused=verified?*(const volatile uint32_t*)(pw_image+PW_PAUSE_COUNTER):1;
    if(!pw_gameplay_allowed(verified,paused)){
        atomic_store(&pw_requested,0);atomic_store(&pw_visual_open,0);return 0;
    }
    return 1;
}
void runtime_power_wheel_status(IssPowerWheelStatus *out){
    *out=(IssPowerWheelStatus){atomic_load(&pw_supported),atomic_load(&pw_ready),
        atomic_load(&pw_active),atomic_load(&pw_mask),atomic_load(&pw_error),atomic_load(&pw_requested)};
}
void runtime_power_wheel_visual(int open,int choice){
    atomic_store(&pw_visual_choice,choice);atomic_store(&pw_visual_open,open&&runtime_power_wheel_gameplay());
}
void runtime_power_wheel_visual_status(int *open,int *choice){
    *open=runtime_power_wheel_gameplay()&&atomic_load(&pw_visual_open);*choice=atomic_load(&pw_visual_choice);
}
/* The existing guarded update bridge calls this independently of Karma state.
 * The overlay and input thread only enqueue; they never call guest code. */
static void pw_tick_impl(void){
    if(!atomic_load(&pw_supported)||!pw_image||pw_inside)return;
    unsigned generation=atomic_load(&pw_generation);
    uintptr_t hero=pw_pointer(pw_image+0x1d0cfe0);
    if(generation!=pw_seen_generation||hero!=pw_hero){
        pw_reset(&pw_bank,generation);pw_seen_generation=generation;pw_hero=hero;
        atomic_store(&pw_requested,0);atomic_store(&pw_error,0);
    }
    PwSample current;
    if(!pw_guest(hero)||!pw_read_game(NULL,&current)||!pw_sample_valid(&current)){
        atomic_store(&pw_ready,0);atomic_store(&pw_requested,0);return;
    }
    int active=pw_index(current.native),mask=0;
    for(int i=0;i<PW_COUNT;i++)if(pw_unlocked(current.unlocked[i]))mask|=1<<i;
    atomic_store(&pw_active,active);atomic_store(&pw_mask,mask);
    atomic_store(&pw_ready,!atomic_load(&pw_error));
    if(!runtime_power_wheel_gameplay())return;
    if(atomic_load(&pw_error))return;
    /* Natural use, drains and native source switches update only the active bank. */
    pw_capture(&pw_bank,active,current.energy,current.maximum,current.missiles);
    if(tlggpu_overlay_captures_input())return;
    int request=atomic_exchange(&pw_requested,0);
    if(!request)return;
    if(generation!=atomic_load(&pw_generation))return;
    PwGame game={NULL,pw_read_game,pw_change_game,pw_write_game};
    pw_inside=1;int result=pw_switch(&pw_bank,&game,request-1);pw_inside=0;
    printf("Power wheel: request=%d result=%d\n",request-1,result);
    if(result<0&&result!=PW_SWITCH_LOCKED){atomic_store(&pw_error,result);atomic_store(&pw_ready,0);}
}
void runtime_power_wheel_tick(void){
    if(pw_inside||atomic_flag_test_and_set(&pw_working))return;
    pw_tick_impl();
    atomic_flag_clear(&pw_working);
}
