// SPDX-License-Identifier: GPL-2.0-or-later
#undef NDEBUG
#include "../src/trophy_save_session.h"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace IssTrophies;
static std::vector<unsigned char> Data(int marker){std::vector<unsigned char>b(131072);b[0]=123;b[500]=marker;return b;}
static void Transfer(SaveSession& s,int fd,const std::vector<unsigned char>& b,bool writing){s.Open(fd,writing,writing?0:b.size());s.Part(fd,b.data(),0,b.size(),writing);s.Close(fd);}
int main(){
 auto root=std::filesystem::temp_directory_path()/("iss-save-trophies-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 SaveSession s;s.Configure(root,"archive");auto early=Data(1),complete=Data(2),saved=Data(3),newgame=Data(4);
 Transfer(s,4,complete,false);assert(s.Bound()&&s.Current().earned.empty());assert(s.Earn(17,100));
 Transfer(s,5,saved,true);assert(s.Current().earned.contains(17));
 Transfer(s,6,early,false);assert(s.Current().earned.empty());
 Transfer(s,7,saved,false);assert(s.Current().earned.contains(17));
 // New Game can reset progression and write an autosave without any save read.
 s.ProgressReset();assert(!s.Bound()&&s.Current().earned.empty());
 Transfer(s,9,newgame,true);assert(s.Bound()&&s.Current().earned.empty());
 Transfer(s,10,saved,false);assert(s.Current().earned.contains(17));
 SaveSession restarted;restarted.Configure(root,"archive");Transfer(restarted,4,saved,false);assert(restarted.Current().earned.contains(17));
 // A newly loaded playthrough cannot inherit any other loaded save's events.
 Transfer(restarted,5,newgame,false);assert(restarted.Current().earned.empty());
 // Partial/stale transfers do not bind or bring back a prior save.
 restarted.Open(6,false,early.size());restarted.Part(6,early.data(),0,100,false);restarted.Close(6);assert(!restarted.Bound());
 restarted.Open(7,false,early.size());restarted.Part(7,early.data(),0,early.size(),false);
 Transfer(restarted,8,newgame,false);restarted.Close(7);assert(restarted.Current().save_identity==SaveSession::Identity(newgame));
 assert(!restarted.Earn(-1,100)&&!restarted.Earn(48,100));
 // Only temporary test fixtures are removed.
 for(const auto& entry:std::filesystem::directory_iterator(root/"save-versions"))std::filesystem::remove(entry.path());
 std::filesystem::remove(root/"save-versions");std::filesystem::remove(root);
 std::cout<<"Exact save isolation, save-write inheritance, restart and stale transfer tests passed\n";
}
