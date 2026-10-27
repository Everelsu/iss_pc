// SPDX-License-Identifier: GPL-2.0-or-later
#undef NDEBUG
#include "../src/trophy_save_session.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <chrono>
using namespace IssTrophies;
static std::vector<unsigned char> Fixture(int good=3,int evil=1,bool duplicate=false){
 std::vector<unsigned char> b(131072);size_t at=0;
 auto put=[&]<class T>(T value){std::memcpy(b.data()+at,&value,sizeof(value));at+=sizeof(value);};
 put(uint32_t(123));at=0x29+222*4;
 put(int32_t(0));put(int32_t(0));put(int32_t(0));put(float(0));put(float(0));
 put(uint32_t(0xbadbad13));put(uint32_t(0));put(int16_t(-1));put(int32_t(duplicate?3:2));
 for(int i=0;i<(duplicate?3:2);i++){put(uint8_t(i==0?good:evil));put(uint8_t(0));put(int16_t(i==0?0:1));put(int16_t(-1));put(uint16_t(0));put(float(1));}
 const char names[]="task_msn_smi_chose_betty\0task_msn_smi_chose_augustine\0";
 put(int32_t(sizeof(names)-1));std::memcpy(b.data()+at,names,sizeof(names)-1);at+=sizeof(names)-1;
 put(uint32_t(0xbadbad13));put(int32_t(0));return b;
}
static void Read(SaveSession& s,const std::vector<unsigned char>& b){s.Open(3,false,b.size());s.Part(3,b.data(),0,b.size(),false);s.Close(3);}
int main(int argc,char** argv){
 assert(CollectibleDataMatches("44e22c731e656f3a","d142c1af440c9df7"));
 assert(!CollectibleDataMatches("different","d142c1af440c9df7"));
 assert(!CollectibleDataMatches("44e22c731e656f3a","different"));
 const int kinds[]={10,10,9,9,23,5,20,8},thresholds[]={102,204,38,76,14,13,15,29},awards[]={25,26,27,28,29,30,31,32};
 for(int i=0;i<8;i++){
  std::array<int32_t,24> counts{};counts[kinds[i]]=thresholds[i]-1;auto before=RecoverCollectibles(counts);assert(std::find(before.begin(),before.end(),awards[i])==before.end());
  counts[kinds[i]]++;auto at=RecoverCollectibles(counts);assert(std::find(at.begin(),at.end(),awards[i])!=at.end());
 }
 assert(RecoveredTrophies(Fixture())==std::vector<int>{2});
 assert(RecoveredTrophies(Fixture(1,1)).empty());
 assert(RecoveredTrophies(Fixture(3,3)).empty());
 assert(RecoveredTrophies(Fixture(3,1,true)).empty());
 const int indices[]={2,5,8,9},ids[]={17,19,22,42};
 for(int i=0;i<4;i++){
  std::array<int32_t,10> counters{};counters[indices[i]]=9;assert(RecoverSavedCounters(counters).empty());
  counters[indices[i]]=10;assert(RecoverSavedCounters(counters)==std::vector<int>{ids[i]});
  counters[indices[i]]=11;assert(RecoverSavedCounters(counters)==std::vector<int>{ids[i]});
 }
 {std::array<int32_t,10> counters{};counters[0]=-1;bool refused=false;try{RecoverSavedCounters(counters);}catch(...){refused=true;}assert(refused);}
 const char* pairs[][2]={{"task_msn_smi_chose_betty","task_msn_smi_chose_augustine"},{"msn_lg","msn_le"},{"msn_vdg","msn_vde"},{"task_msn_sam_good_choice","task_msn_sam_evil_choice"},{"task_msn_cbc_started_good","task_msn_cbc_started_evil"}};
 for(int pair=0;pair<5;pair++)for(int branch=0;branch<2;branch++){
  std::map<std::string,StoryTask> t;
  t[pairs[pair][branch]]={3,0,-1,0,1};t[pairs[pair][1-branch]]={0,0,-1,0,0};
  if(pair>=3){assert(RecoverStoryTasks(t).empty());t[pair==3?"msn_sam":"msn_cb"]={3,0,-1,0,1};}
  assert(RecoverStoryTasks(t)==std::vector<int>{2+pair*2+branch});
  t[pairs[pair][1-branch]].state=3;assert(RecoverStoryTasks(t).empty());
  t[pairs[pair][1-branch]].state=255;assert(RecoverStoryTasks(t).empty());
  t.erase(pairs[pair][1-branch]);assert(RecoverStoryTasks(t).empty());
 }
 auto bad=Fixture();bad[0]=122;bool refused=false;try{RecoveredTrophies(bad);}catch(...){refused=true;}assert(refused);
 bad.resize(100);refused=false;try{RecoveredTrophies(bad);}catch(...){refused=true;}assert(refused);
 for(size_t offset:{size_t(0x29+222*4),size_t(0x29+222*4+20),size_t(0x29+222*4+34)}){
  bad=Fixture();bad[offset]=255;refused=false;try{RecoveredTrophies(bad);}catch(...){refused=true;}assert(refused);
 }
 auto root=std::filesystem::temp_directory_path()/("iss-recovery-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 SaveSession s;s.Configure(root,"archive");s.EnableRecovery(true);auto data=Fixture();Read(s,data);
 assert(s.RecoveryProblem().empty()&&s.Current().earned.contains(2)&&s.Current().recovered.contains(2));
 SaveSession restart;restart.Configure(root,"archive");restart.EnableRecovery(true);Read(restart,data);assert(restart.Current().recovered.contains(2));
 // Actual events keep their provenance, even if a matching recovery rule exists.
 auto key=SaveSession::Identity(data);Progress live{"archive",{{2,100}},key,{}};Save(s.Path(key),live);Read(restart,data);assert(restart.Current().earned.at(2)==100&&restart.Current().recovered.empty());
 Read(restart,Fixture(1,1));assert(restart.Current().earned.empty());
 // Unsupported progression data does not borrow another save's awards.
 bad=Fixture(1,1);bad[0x29+222*4]=255;Read(restart,bad);assert(restart.Bound()&&restart.Current().earned.empty()&&!restart.RecoveryProblem().empty());
 for(int i=1;i<argc;i++){
  std::ifstream f(std::filesystem::path(argv[i]),std::ios::binary);assert(f);std::vector<unsigned char>b((std::istreambuf_iterator<char>(f)),{});
  auto found=RecoveredTrophies(b);std::cout<<argv[i]<<": ";for(int id:found)std::cout<<id<<' ';std::cout<<'\n';
  if(i==2)assert((found==std::vector<int>{2,4,6,8,10,17,42}));else if(i<=3)assert(found==std::vector<int>{2});else assert(found.empty());
  auto all=RecoveredTrophies(b,true);
  if(i==2)assert((all==std::vector<int>{2,4,6,8,10,17,25,26,27,28,29,30,31,32,42}));else assert(all==found);
  // Guard-off fallback never adds collectible awards. Guard-on writes only
  // this exact save's temporary ledger, and survives a new session object.
  s.EnableCollectibleRecovery(true);Read(s,b);assert(s.Current().earned.size()==all.size());
  SaveSession again;again.Configure(root,"archive");again.EnableRecovery(true);again.EnableCollectibleRecovery(true);Read(again,b);assert(again.Current().earned==s.Current().earned);
 }
 for(const auto& e:std::filesystem::directory_iterator(root/"save-versions"))std::filesystem::remove(e.path());std::filesystem::remove(root/"save-versions");std::filesystem::remove(root);
 std::cout<<"Recovery rules, refusal, restart, provenance and save isolation passed\n";
}
