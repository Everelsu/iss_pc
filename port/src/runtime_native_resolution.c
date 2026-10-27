#include "iss_native_resolution.h"
#include <stdatomic.h>
static atomic_int native_status;
static _Atomic(const char *) native_reason="Disabled";
static IssNativePlan active_plan;
static atomic_flag plan_lock=ATOMIC_FLAG_INIT;
static _Thread_local IssNativePlan plan_snapshot;
static void lock_plan(void){while(atomic_flag_test_and_set_explicit(&plan_lock,memory_order_acquire)){} }
static void unlock_plan(void){atomic_flag_clear_explicit(&plan_lock,memory_order_release);}
void runtime_native_publish(int status,const char *reason,const IssNativePlan *plan) {
    lock_plan();
    if(plan)active_plan=*plan;else memset(&active_plan,0,sizeof(active_plan));
    atomic_store(&native_reason,reason);
    atomic_store_explicit(&native_status,status,memory_order_release);
    unlock_plan();
}
int runtime_native_status(void){return atomic_load_explicit(&native_status,memory_order_acquire);}
const char *runtime_native_reason(void){return atomic_load(&native_reason);}
const IssNativePlan *runtime_native_plan(void){lock_plan();plan_snapshot=active_plan;unlock_plan();return &plan_snapshot;}
float runtime_native_safe_area(void){
    const IssNativePlan *p=runtime_native_plan();
    return runtime_native_status()==1 && !p->hud_edges && p->ui_width ? 1920.0f/p->ui_width : 1.0f;
}
