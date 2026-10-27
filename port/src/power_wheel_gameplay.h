#ifndef ISS_POWER_WHEEL_GAMEPLAY_H
#define ISS_POWER_WHEEL_GAMEPLAY_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* Same US 1.07 counter and RIP-relative guard used by gpu/pause_guard.h.
 * Independent of the renderer's diagnostic enable flag and cooldown. */
#define PW_PAUSE_COUNTER 0x1d1f7fcu
#define PW_PAUSE_SITE 0x27db27u
static inline int pw_pause_signature(const unsigned char *image,size_t size){
    if(!image || size<PW_PAUSE_COUNTER+sizeof(uint32_t))return 0;
    const unsigned char *s=image+PW_PAUSE_SITE;
    int32_t displacement;memcpy(&displacement,s+3,4);
    return s[0]==0x4c && s[1]==0x8d && s[2]==0x35 &&
        (int64_t)PW_PAUSE_SITE+7+displacement==PW_PAUSE_COUNTER;
}
static inline int pw_gameplay_allowed(int verified,uint32_t pause_count){
    return verified && pause_count==0;
}
#endif
