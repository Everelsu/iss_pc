/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/iss_native_target_policy.h"
#include <cassert>
#include <cstdio>
int main() {
    const uint32_t down[]={3440,1440,2580,1080},up[]={2580,1080,3440,1440};
    // Returning to a formerly visited size must not preserve the old family.
    for(unsigned k=0;k<=4;k++) {
        assert(iss_native_retire_target(3440>>k,1440>>k,down,0,0));
        assert(iss_native_retire_target(2580>>k,1080>>k,up,0,0));
    }
    assert(!iss_native_retire_target(3440,1440,up,0,0));
    assert(!iss_native_retire_target(2580,1080,down,0,0));
    assert(!iss_native_retire_target(3440,1440,down,1,0));
    assert(!iss_native_retire_target(3440,1440,down,0,1));
    assert(!iss_native_retire_target(768,768,down,0,0));
    assert(!iss_native_retire_target(2048,2048,down,0,0));
    assert(!iss_native_retire_target(1920,1080,down,0,0));
    assert(!iss_native_retire_target(0,0,down,0,0));
    // A new size equal to an old half-size is shared, not stale.
    const uint32_t shared[]={3440,1440,1720,720};
    assert(!iss_native_retire_target(1720,720,shared,0,0));
    assert(iss_native_retire_target(3448,1448,down,0,0));
    assert(!iss_native_retire_target(3449,1449,down,0,0));
    puts("Native resize retirement: return visits, shared families, used/display/shadow/UI protection passed");
}
