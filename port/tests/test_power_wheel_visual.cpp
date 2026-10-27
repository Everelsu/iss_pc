#include "power_wheel_visual.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "power_wheel_art_mask.h"
int main(){
    // Exhaustive canvas samples: highlights never enter another quadrant.
    for(int y=-256;y<=256;y++)for(int x=-256;x<=256;x++){
        int lit=0;
        for(int sector=0;sector<4;sector++){
            float mask=pw_art_sector_mask(sector,x/512.0f,y/512.0f);
            assert(mask>=0&&mask<=1);if(mask>0)lit++;
        }
        assert(lit<=1);
    }
    for(int sector=0;sector<4;sector++){
        assert(pw_art_sector_mask(sector,.3f,.3f)==0);
        assert(pw_art_sector_mask(sector,-.3f,.3f)==0);
        assert(pw_art_sector_mask(sector,NAN,0)==0);
    }
    assert(pw_art_layer(0,0)==0&&pw_art_layer(0,.49f)==0);
    assert(pw_art_layer(1,.477f)==1&&pw_art_layer(2,.477f)==1);
    assert(pw_art_layer(2,.447f)>pw_art_layer(2,.420f));
    for(int layer=0;layer<3;layer++)assert(pw_art_layer(layer,NAN)==0);
    PwHoverFade f{-1,0};pw_hover_step(&f,0,0.09f);assert(f.shown==0&&fabsf(f.amount-1)<0.001f);
    pw_hover_step(&f,2,0.045f);assert(f.shown==0&&fabsf(f.amount-0.5f)<0.001f);
    pw_hover_step(&f,2,0.135f);assert(f.shown==2&&fabsf(f.amount-1)<0.001f);
    pw_hover_step(&f,-1,0.09f);assert(f.shown==-1&&f.amount==0);
    PwHoverFade steps{1,1};for(int i=0;i<18;i++)pw_hover_step(&steps,3,0.01f);
    assert(steps.shown==3&&fabsf(steps.amount-1)<0.001f);
    auto old=steps;pw_hover_step(&steps,0,NAN);assert(steps.shown==old.shown&&steps.amount==old.amount);
    assert(pw_art_hit(0,0)==-1&&pw_art_hit(.5f,0)==-1&&pw_art_hit(.3f,.3f)==-1);
    const float dx[]={-.3f,0,.3f,0},dy[]={0,-.3f,0,.3f};
    for(float side:{360.0f,600.0f,1200.0f})for(int i=0;i<4;i++){
        float px=2000+dx[i]*side,py=1000+dy[i]*side;
        assert(pw_art_hit((px-2000)/side,(py-1000)/side)==i);
    }
    puts("Wheel visual: 180 ms one-sector transition, frame-rate invariance, hub/gap bounds and scale invariance passed.");
}
