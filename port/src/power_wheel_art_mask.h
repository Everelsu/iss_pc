#ifndef ISS_POWER_WHEEL_ART_MASK_H
#define ISS_POWER_WHEEL_ART_MASK_H
#include <math.h>
/* Normalized source canvas: the approved 1254-square painted wheel.
 * Leave a separator margin; halo and fill share this exact quadrant mask. */
static inline float pw_art_sector_mask(int sector,float x,float y){
    if(sector<0||sector>3||!isfinite(x)||!isfinite(y))return 0;
    float axis=sector==0?-x:sector==1?-y:sector==2?x:y;
    float cross=(sector==0||sector==2)?fabsf(y):fabsf(x);
    return fminf(1.0f,fmaxf(0.0f,(axis-cross-0.008f)/0.008f));
}
/* Separate alpha layers keep a low-opacity fill independent of the glow slider. */
static inline float pw_art_layer(int layer,float radius){
    if(!isfinite(radius))return 0;
    if(layer==0)return fminf(1.0f,fmaxf(0.0f,(radius-0.108f)/0.012f))*
        fminf(1.0f,fmaxf(0.0f,(0.470f-radius)/0.015f));
    if(layer==1)return expf(-powf((radius-0.477f)/0.010f,2.0f));
    if(layer==2)return expf(-powf((radius-0.477f)/0.024f,2.0f));
    return 0;
}
#endif
