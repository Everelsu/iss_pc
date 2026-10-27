#ifndef ISS_POWER_WHEEL_VISUAL_H
#define ISS_POWER_WHEEL_VISUAL_H
#include <math.h>
typedef struct { int shown; float amount; } PwHoverFade;
static inline void pw_hover_step(PwHoverFade *s,int target,float dt){
    if(!isfinite(dt)||dt<0)return;
    if(target<-1||target>3)target=-1;
    /* A 90 ms fade-out followed by a 90 ms fade-in lights only one sector. */
    if(target!=s->shown){
        float needed=s->amount*0.09f;
        if(dt<needed){s->amount-=dt/0.09f;return;}
        dt-=needed;s->amount=0;s->shown=target;
    }
    if(target>=0)s->amount=fminf(1.0f,s->amount+dt/0.09f);
    else s->amount=0;
}
static inline int pw_art_hit(float x,float y){
    if(!isfinite(x)||!isfinite(y))return -1;
    float r=sqrtf(x*x+y*y);
    if(r<0.112f||r>0.466f)return -1;
    if(fabsf(fabsf(x)-fabsf(y))<r*0.025f)return -1;
    if(fabsf(x)>fabsf(y))return x<0?0:2;
    return y<0?1:3;
}
#endif
