#include "power_wheel_input.h"
#include "power_wheel_gameplay.h"
#include <vector>
#include <cassert>
#include <cstdio>
int main(){
    std::vector<unsigned char> image(PW_PAUSE_COUNTER+4);
    auto site=image.data()+PW_PAUSE_SITE;
    site[0]=0x4c;site[1]=0x8d;site[2]=0x35;
    int32_t disp=PW_PAUSE_COUNTER-(PW_PAUSE_SITE+7);memcpy(site+3,&disp,4);
    assert(pw_pause_signature(image.data(),image.size()));
    assert(!pw_pause_signature(nullptr,image.size()));
    assert(!pw_pause_signature(image.data(),PW_PAUSE_COUNTER+3));
    site[3]^=1;assert(!pw_pause_signature(image.data(),image.size()));site[3]^=1;
    site[0]^=1;assert(!pw_pause_signature(image.data(),image.size()));site[0]^=1;
    assert(pw_gameplay_allowed(1,0));
    assert(!pw_gameplay_allowed(0,0)&&!pw_gameplay_allowed(1,1)&&!pw_gameplay_allowed(1,2));
    // Entering a menu cancels a visible selection without consuming menu input.
    // Holding Down across resume requires release before a fresh wheel gesture.
    for(int instant=0;instant<2;instant++){
        PwHold menu{};
        pw_hold_mode(&menu,0,1,1,0,0,instant,15);
        auto visible=pw_hold_mode(&menu,300000,1,1,1,0,instant,15);assert(visible.open);
        auto paused=pw_hold_mode(&menu,310000,pw_gameplay_allowed(1,1),1,1,0,instant,15);
        assert(!paused.open&&!paused.consume_down&&!paused.consume_stick&&!paused.tap&&paused.request==-1);
        auto resumed=pw_hold_mode(&menu,400000,1,1,1,0,instant,15);
        assert(!resumed.open&&resumed.request==-1);
        pw_hold_mode(&menu,410000,1,0,0,0,instant,15);
        pw_hold_mode(&menu,420000,1,1,0,0,instant,15);
        assert(pw_hold_mode(&menu,720000,1,1,1,0,instant,15).open);
    }
    PwHold h{};
    auto r=pw_hold(&h,0,1,1,0,0);assert(r.consume_down&&!r.open);
    r=pw_hold(&h,100000,1,0,0,0);assert(r.tap&&r.request==-1);
    pw_hold(&h,200000,1,1,0,0);
    r=pw_hold(&h,450000,1,1,1,0);assert(r.open&&r.consume_stick&&r.choice==PW_VIDEO);
    r=pw_hold(&h,460000,1,0,0,0);assert(r.request==PW_VIDEO&&!r.tap&&r.consume_stick);
    pw_hold(&h,500000,1,1,0,0);pw_hold(&h,800000,1,1,-1,0);
    pw_hold(&h,810000,1,1,0,0);
    pw_hold(&h,1170000,1,1,0,0);
    r=pw_hold(&h,1180000,1,0,0,0);assert(r.request==-1&&!r.tap);
    pw_hold(&h,900000,0,1,1,0);
    r=pw_hold(&h,1200000,1,1,1,0);assert(!r.open&&r.request==-1);
    pw_hold(&h,1300000,1,0,0,0);
    pw_hold(&h,1400000,1,1,0,0);r=pw_hold(&h,1700000,1,1,0,-1);assert(r.choice==PW_NEON);
    r=pw_hold(&h,1800000,0,0,0,0);assert(r.request==-1);
    h={};pw_hold(&h,2000000,1,1,0,0);pw_hold(&h,2300000,1,1,1,0);
    pw_hold(&h,2310000,1,1,0,0);
    r=pw_hold(&h,2320000,1,0,0,0);assert(r.request==PW_VIDEO);
    h={};r=pw_hold_mode(&h,0,1,1,1,0,1,15);
    assert(r.request==PW_VIDEO&&r.consume_stick&&!r.open);
    r=pw_hold_mode(&h,10000,1,1,-1,0,1,15);assert(r.request==-1);
    r=pw_hold_mode(&h,20000,1,0,0,0,1,15);assert(r.request==-1&&!r.tap);
    r=pw_hold_mode(&h,30000,1,1,0,-1,1,15);assert(r.request==PW_NEON);
    pw_hold_mode(&h,40000,1,0,0,0,1,15);
    r=pw_hold_mode(&h,50000,1,1,1,0,1,1);assert(r.request==-1);
    r=pw_hold_mode(&h,60000,1,1,-1,0,1,1);assert(r.request==PW_SMOKE);
    pw_hold_mode(&h,70000,1,0,0,0,1,15);
    pw_hold_mode(&h,80000,1,1,0,0,1,15);
    r=pw_hold_mode(&h,90000,1,0,0,0,1,15);assert(r.tap&&r.request==-1);
    // Quick instant selection must not lock a later visible-wheel release.
    h={};r=pw_hold_mode(&h,100000,1,1,-1,0,1,15);assert(r.request==PW_SMOKE);
    r=pw_hold_mode(&h,400000,1,1,1,0,1,15);assert(r.open&&r.choice==PW_VIDEO&&r.request==-1);
    r=pw_hold_mode(&h,450000,1,1,0,1,1,15);assert(r.choice==PW_CONCRETE&&r.request==-1);
    r=pw_hold_mode(&h,460000,1,0,0,0,1,15);assert(r.request==PW_CONCRETE);
    // A deliberate long hold browses without changing power until release.
    h={};pw_hold_mode(&h,500000,1,1,0,0,1,15);
    r=pw_hold_mode(&h,800000,1,1,0,-1,1,15);assert(r.open&&r.request==-1&&r.choice==PW_NEON);
    r=pw_hold_mode(&h,810000,1,0,1,0,1,15);assert(r.request==PW_VIDEO);
    h={};pw_hold_mode(&h,900000,1,1,0,0,1,15);pw_hold_mode(&h,1200000,1,1,1,0,1,15);
    pw_hold_mode(&h,1210000,1,1,0,0,1,15);pw_hold_mode(&h,1600000,1,1,0,0,1,15);
    r=pw_hold_mode(&h,1610000,1,0,0,0,1,15);assert(r.request==-1);
    puts("Wheel input: tap passthrough, hold threshold, release selection, center cancel and focus/menu cancellation passed.");
}
