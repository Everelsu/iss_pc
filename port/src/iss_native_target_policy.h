/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>

/* Adapted from axylpup/shadPS4 second-son-v1.1, texture_cache.cpp
 * PurgeOldRenderSize (GPL-2.0-or-later). Only the old family, never the
 * new family's members, used images or display surfaces are eligible. */
static inline int iss_native_scene_family(uint32_t w,uint32_t h,uint32_t scene_w,uint32_t scene_h) {
    if (!w || !h || !scene_w || !scene_h) return 0;
    for (unsigned shift=0;shift<=4;shift++) {
        int64_t dw=(int64_t)w-(scene_w>>shift),dh=(int64_t)h-(scene_h>>shift);
        if(dw>=-8 && dw<=8 && dh>=-8 && dh<=8)return 1;
    }
    return 0;
}
static inline int iss_native_retire_target(uint32_t w,uint32_t h,const uint32_t sizes[4],int used,int display) {
    return !used && !display && iss_native_scene_family(w,h,sizes[0],sizes[1]) &&
        !iss_native_scene_family(w,h,sizes[2],sizes[3]);
}
