#ifndef ISS_POWER_WHEEL_SIGNATURES_H
#define ISS_POWER_WHEEL_SIGNATURES_H
#include <stdint.h>
#include <stddef.h>
typedef struct { uint32_t offset, length, hash; } PwCodeCheck;
static const PwCodeCheck pw_code_checks[]={
    {0x2143d0,0x158,0x48eb0971u},
    {0x2064e0,0xcf,0x5473c4e4u},
    {0x1e35a0,0x4c,0x8b83547eu},
    {0x1e35f0,0x7,0x87731340u},
    {0x1e3600,0x4c,0xa30b846eu},
};
static inline int pw_signatures(const unsigned char *image,size_t size){
    if(!image)return 0;
    for(size_t i=0;i<sizeof(pw_code_checks)/sizeof(pw_code_checks[0]);i++){
        const PwCodeCheck *p=&pw_code_checks[i];
        if(p->offset>size || p->length>size-p->offset)return 0;
        uint32_t hash=2166136261u;
        for(uint32_t j=0;j<p->length;j++)hash=(hash^image[p->offset+j])*16777619u;
        if(hash!=p->hash)return 0;
    }
    return 1;
}
#endif
