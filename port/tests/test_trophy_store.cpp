// SPDX-License-Identifier: GPL-2.0-or-later
#undef NDEBUG
#include "../src/trophy_store.h"
#include <cassert>
#include <fstream>
#include <functional>
#include <chrono>
#include <iostream>
using namespace IssTrophies;
static void Reject(std::function<void()> action){bool failed=false;try{action();}catch(...){failed=true;}assert(failed);}
static void Put(std::vector<unsigned char>& b,int p,uint64_t v,int n){while(n--){b[p+n]=v&255;v>>=8;}}
static std::vector<unsigned char> Archive(){
    std::vector<unsigned char>b(168);Put(b,0,0xdca24d00,4);Put(b,4,3,4);Put(b,8,b.size(),8);Put(b,16,1,4);Put(b,20,64,4);
    const char name[]="TROP000.PNG";std::copy(name,name+sizeof(name),b.begin()+96);Put(b,128,160,8);Put(b,136,8,8);return b;
}
int main(int argc,char** argv){
    auto b=Archive();assert(ParseTrp(b).size()==1);auto copy=b;
    b[0]=0;Reject([&]{ParseTrp(b);});b=copy;Put(b,136,1000,8);Reject([&]{ParseTrp(b);});
    b=copy;Put(b,128,100,8);Reject([&]{ParseTrp(b);});b=copy;b[96]='/';Reject([&]{ParseTrp(b);});
    b=copy;Put(b,16,1025,4);Reject([&]{ParseTrp(b);});b=copy;b.pop_back();Reject([&]{ParseTrp(b);});
    auto root=std::filesystem::temp_directory_path()/std::filesystem::path("iss-trophy-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto path=root/"earned.json";auto p=Load(path,"test",48);assert(p.earned.empty());p.earned[17]=123456;Save(path,p);
    auto q=Load(path,"test",48);assert(q.earned.at(17)==123456);Save(path,q);assert(std::filesystem::exists(path.string()+".bak"));
    Reject([&]{Load(path,"other-game",48);});Reject([&]{Load(path,"test",10);});
    {std::ofstream bad(path);bad<<"broken";}Reject([&]{Load(path,"test",48);});
    // Test fixtures only, no game files or normal user profile involved.
    std::filesystem::remove(path);std::filesystem::remove(path.string()+".bak");std::filesystem::remove(root);
    if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<unsigned char> actual((std::istreambuf_iterator<char>(f)),{});auto entries=ParseTrp(actual);assert(entries.size()==73);std::cout<<"Real game: "<<entries.size()<<" bounded entries, fingerprint "<<Fingerprint(actual)<<'\n';}
    std::cout<<"Trophy archive, corruption, identity, backup and persistence checks passed\n";
}
