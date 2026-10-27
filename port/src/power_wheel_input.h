#ifndef ISS_POWER_WHEEL_INPUT_H
#define ISS_POWER_WHEEL_INPUT_H
#include "power_wheel_state.h"
typedef struct { uint64_t start,center_since; int held,open,choice,blocked,fired; } PwHold;
typedef struct { int consume_down,consume_stick,tap,request,open,choice; } PwHoldResult;
static inline PwHoldResult pw_hold_mode(PwHold *s,uint64_t now,int allowed,int down,float x,float y,int instant,unsigned unlocked){
    PwHoldResult r={0,0,0,-1,0,-1};
    if(!allowed){memset(s,0,sizeof(*s));s->choice=-1;s->blocked=down;return r;}
    if(s->blocked){s->blocked=down;return r;}
    if(down){
        if(!s->held){s->held=1;s->start=now;s->choice=-1;s->center_since=0;s->fired=0;}
        if(now>=s->start&&now-s->start>=250000)s->open=1;
        int direction=pw_direction(x,y);
        if(instant&&!s->open){
            if(!s->fired&&direction>=0&&(unlocked&(1u<<direction))){
                s->choice=direction;s->fired=1;r.request=direction;
            }
        }
        if(s->open){
            if(direction>=0){s->choice=direction;s->center_since=0;}
            else {
                if(!s->center_since)s->center_since=now;
                /* Brief centering while releasing should not lose a selection.
                 * Deliberately hold center for 350 ms to cancel. */
                if(now>=s->center_since&&now-s->center_since>=350000)s->choice=-1;
            }
        }
        r.consume_down=1;r.consume_stick=instant||s->open;r.open=s->open;r.choice=s->choice;
    }else if(s->held){
        if(s->open){
            int direction=pw_direction(x,y);
            r.request=direction>=0?direction:s->choice;
        }
        else if(!s->open&&!s->fired)r.tap=1;
        r.consume_stick=instant||s->open;s->held=0;s->open=0;s->choice=-1;s->fired=0;
    }
    return r;
}
static inline PwHoldResult pw_hold(PwHold *s,uint64_t now,int allowed,int down,float x,float y){
    return pw_hold_mode(s,now,allowed,down,x,y,0,15);
}
#endif
