#ifndef ISS_POWER_WHEEL_TRANSACTION_H
#define ISS_POWER_WHEEL_TRANSACTION_H
#include "power_wheel_state.h"

typedef struct { int native, missiles, capacity; float energy, maximum; uint32_t unlocked[PW_COUNT]; } PwSample;
typedef struct {
    void *context;
    int (*read)(void *,PwSample *);
    int (*change)(void *,int);
    int (*resources)(void *,float,int);
} PwGame;
enum { PW_SWITCH_OK=1, PW_SWITCH_INVALID=-1, PW_SWITCH_LOCKED=-2,
       PW_SWITCH_CHANGE_FAILED=-3, PW_SWITCH_RESTORE_FAILED=-4 };
static inline int pw_sample_layout_valid(const PwSample *s){
    /* The native changer updates capacities before replacing old resources.
     * Permit bounded source leftovers only while applying the new balance. */
    return pw_index(s->native)>=0 && isfinite(s->energy) && isfinite(s->maximum) &&
        s->maximum>0 && s->maximum<=100000 && s->energy>=0 && s->energy<=100000 &&
        s->missiles>=0 && s->missiles<=1000 && s->capacity>=0 && s->capacity<=1000;
}
static inline int pw_sample_valid(const PwSample *s){
    return pw_sample_layout_valid(s) && s->energy<=s->maximum+0.001f && s->missiles<=s->capacity;
}
/* Run only on the game update thread. No absorb/refill operation is exposed.
 * A failed change does not overwrite the bank; a failed resource restore must
 * disable further requests in the adapter and be reported to the player. */
static inline int pw_switch(PwBank *bank,const PwGame *game,int target){
    if(!bank||!game||!game->read||!game->change||!game->resources||target<0||target>=PW_COUNT)
        return PW_SWITCH_INVALID;
    PwSample before,after;memset(&before,0,sizeof(before));memset(&after,0,sizeof(after));
    if(!game->read(game->context,&before)||!pw_sample_valid(&before))return PW_SWITCH_INVALID;
    if(!pw_unlocked(before.unlocked[target]))return PW_SWITCH_LOCKED;
    int source=pw_index(before.native);
    if(source==target)return PW_SWITCH_OK;
    PwBank next=*bank;
    if(!pw_capture(&next,source,before.energy,before.maximum,before.missiles))return PW_SWITCH_INVALID;
    /* Carry the current balance into every destination. Returning to a power
     * must never recover its older, higher balance after spending elsewhere. */
    next.resource[target]=next.resource[source];
    if(!game->change(game->context,pw_native_type[target]))return PW_SWITCH_CHANGE_FAILED;
    if(!game->read(game->context,&after)||!pw_sample_layout_valid(&after)||after.native!=pw_native_type[target])
        return PW_SWITCH_CHANGE_FAILED;
    float energy;int missiles;
    if(!pw_restore(&next,target,after.maximum,after.capacity,&energy,&missiles)||
       !game->resources(game->context,energy,missiles))return PW_SWITCH_RESTORE_FAILED;
    PwSample verified;memset(&verified,0,sizeof(verified));
    if(!game->read(game->context,&verified)||!pw_sample_valid(&verified)||
       verified.native!=after.native||verified.missiles!=missiles||fabsf(verified.energy-energy)>0.001f)
        return PW_SWITCH_RESTORE_FAILED;
    *bank=next;
    return PW_SWITCH_OK;
}
#endif
