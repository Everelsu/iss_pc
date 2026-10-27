#ifndef ISS_POWER_WHEEL_STATE_H
#define ISS_POWER_WHEEL_STATE_H
#include <math.h>
#include <stdint.h>
#include <string.h>
enum { PW_SMOKE, PW_NEON, PW_VIDEO, PW_CONCRETE, PW_COUNT };
static const int pw_native_type[PW_COUNT]={1,0,3,4};
static const int pw_family_id[PW_COUNT]={17,77,139,215};
typedef struct { float fraction; int missiles,known; } PwResource;
typedef struct { PwResource resource[PW_COUNT]; uint64_t session; } PwBank;
static inline int pw_index(int native){for(int i=0;i<PW_COUNT;i++)if(pw_native_type[i]==native)return i;return -1;}
static inline int pw_unlocked(uint32_t flags){return (flags&5)==5 && !(flags&16);}
static inline void pw_reset(PwBank *bank,uint64_t session){memset(bank,0,sizeof(*bank));bank->session=session;}
static inline int pw_capture(PwBank *bank,int index,float energy,float maximum,int missiles){
    if(index<0||index>=PW_COUNT||!isfinite(energy)||!isfinite(maximum)||maximum<=0||
        energy<0||energy>maximum+0.001f||missiles<0||missiles>1000)return 0;
    bank->resource[index]=(PwResource){fminf(1.0f,energy/maximum),missiles,1};return 1;
}
/* A never-observed bank starts empty: selecting it must not create resources.
 * Capacity reductions clamp ammunition; capacity increases never add missiles. */
static inline int pw_restore(const PwBank *bank,int index,float maximum,int missile_capacity,float *energy,int *missiles){
    if(index<0||index>=PW_COUNT||!isfinite(maximum)||maximum<=0||missile_capacity<0||missile_capacity>1000)return 0;
    const PwResource *r=&bank->resource[index];
    if(r->known&&(!isfinite(r->fraction)||r->fraction<0||r->fraction>1||r->missiles<0))return 0;
    *energy=r->known?r->fraction*maximum:0;
    *missiles=r->known?(r->missiles<missile_capacity?r->missiles:missile_capacity):0;return 1;
}
/* SDL stick coordinates: right/down positive; cardinal selection with a
 * radial dead zone, center cancels. No camera input is synthesized here. */
static inline int pw_direction(float x,float y){
    if(!isfinite(x)||!isfinite(y)||x*x+y*y<0.16f)return -1;
    if(fabsf(x)>fabsf(y))return x<0?PW_SMOKE:PW_VIDEO;
    return y<0?PW_NEON:PW_CONCRETE;
}
#endif
