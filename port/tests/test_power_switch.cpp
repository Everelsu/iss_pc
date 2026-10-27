#include "power_wheel_transaction.h"
#include "power_wheel_signatures.h"
#include <cassert>
#include <cstdio>
#include <vector>
struct Mock { PwSample sample; int changes=0, writes=0, fail_change=0, fail_write=0, next_capacity=-1; float next_maximum=0; };
static int read_game(void *p,PwSample *out){*out=static_cast<Mock*>(p)->sample;return 1;}
static int change_game(void *p,int native){auto &m=*static_cast<Mock*>(p);m.changes++;if(m.fail_change)return 0;m.sample.native=native;
    if(m.next_capacity>=0)m.sample.capacity=m.next_capacity;
    if(m.next_maximum>0)m.sample.maximum=m.next_maximum;
    return 1;}
static int write_game(void *p,float energy,int missiles){auto &m=*static_cast<Mock*>(p);m.writes++;if(m.fail_write)return 0;m.sample.energy=energy;m.sample.missiles=missiles;return 1;}
int main(){
    // Live Concrete regression: the changer leaves 130 energy / 5 missiles
    // temporarily present while capacities have already become 104 / 3.
    Mock concrete{};concrete.sample={3,5,5,130,130,{39,39,39,39}};
    concrete.next_capacity=3;concrete.next_maximum=104;
    PwGame concrete_game{&concrete,read_game,change_game,write_game};PwBank concrete_bank{};
    assert(pw_switch(&concrete_bank,&concrete_game,PW_CONCRETE)==PW_SWITCH_OK);
    assert(concrete.sample.native==4&&concrete.sample.energy==104&&concrete.sample.missiles==3);
    concrete.next_capacity=5;concrete.next_maximum=130;
    assert(pw_switch(&concrete_bank,&concrete_game,PW_SMOKE)==PW_SWITCH_OK);
    assert(concrete.sample.native==1&&concrete.sample.energy==130&&concrete.sample.missiles==3);
    Mock m{};m.sample={1,2,5,35,100,{39,39,39,39}};
    PwGame game{&m,read_game,change_game,write_game};PwBank bank{};
    assert(pw_switch(&bank,&game,PW_NEON)==PW_SWITCH_OK);
    assert(m.sample.energy==35&&m.sample.missiles==2);
    m.sample.energy=20;m.sample.missiles=1;
    assert(pw_switch(&bank,&game,PW_SMOKE)==PW_SWITCH_OK);
    assert(m.sample.energy==20&&m.sample.missiles==1);
    // A normal source refill carries forward; destination capacity clamps ammo.
    m.sample.energy=100;m.sample.missiles=5;m.next_capacity=3;m.next_maximum=80;
    assert(pw_switch(&bank,&game,PW_NEON)==PW_SWITCH_OK);
    assert(m.sample.energy==80&&m.sample.missiles==3);
    m.next_capacity=5;m.next_maximum=100;
    assert(pw_switch(&bank,&game,PW_SMOKE)==PW_SWITCH_OK);
    assert(m.sample.energy==100&&m.sample.missiles==3);
    m.sample.energy=0;m.sample.missiles=0;
    assert(pw_switch(&bank,&game,PW_NEON)==PW_SWITCH_OK);
    assert(m.sample.energy==0&&m.sample.missiles==0);
    assert(pw_switch(&bank,&game,PW_SMOKE)==PW_SWITCH_OK);
    int changes=m.changes,writes=m.writes;
    assert(pw_switch(&bank,&game,PW_SMOKE)==PW_SWITCH_OK&&m.changes==changes&&m.writes==writes);
    m.sample.unlocked[PW_VIDEO]=0;
    assert(pw_switch(&bank,&game,PW_VIDEO)==PW_SWITCH_LOCKED&&m.changes==changes);
    m.fail_change=1;auto saved=bank;
    assert(pw_switch(&bank,&game,PW_NEON)==PW_SWITCH_CHANGE_FAILED&&!memcmp(&saved,&bank,sizeof(bank)));
    m.fail_change=0;m.fail_write=1;
    assert(pw_switch(&bank,&game,PW_NEON)==PW_SWITCH_RESTORE_FAILED);
    assert(!pw_signatures(nullptr,0));
    std::vector<unsigned char> image(0x214528);
    assert(!pw_signatures(image.data(),image.size()));
    assert(!pw_signatures(image.data(),0x100));
    puts("Power transaction: carry current resources, preserve spending, normal refill, locked/same-power requests, failed calls and signature rejection passed.");
}
