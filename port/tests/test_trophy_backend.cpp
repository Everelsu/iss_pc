// SPDX-License-Identifier: GPL-2.0-or-later
#undef NDEBUG
#include "../src/iss_trophies.h"
#include <filesystem>
#include <fstream>
#include <cassert>
#include <cstring>
#include <string>
#include <iostream>
#include <bit>
#include <cstdlib>
static const IssTrophyBackend* backend;
static std::string archive,user;
static bool actualWorld=false,missingWorld=false;
extern "C" void runtime_trophy_set_backend(const IssTrophyBackend* value){backend=value;}
extern "C" int runtime_trophy_save_mode_ready(){return 1;}
extern "C" const char* runtime_file_user_dir(){return user.c_str();}
extern "C" int runtime_file_translate(const char* guest,char* out,size_t size){
    std::string path=archive;
    if(std::string(guest).starts_with("/app0/art/")){
        if(missingWorld)return -1;
        if(actualWorld)path=(std::filesystem::path(archive).parent_path().parent_path().parent_path()/std::string(guest).substr(6)).string();
    }
    if(path.size()+1>size)return -1;std::memcpy(out,path.c_str(),path.size()+1);return 0;
}
int main(int argc,char** argv){
    assert((argc==4||argc==5)&&backend);archive=argv[1];user=argv[2];std::string mode=argv[3];
    actualWorld=mode=="collectibles";missingWorld=mode=="fallback";
    if(actualWorld||missingWorld){
        assert(argc==5);
#ifdef _WIN32
        _putenv_s("ISS_TROPHIES_PER_SAVE","1");
#else
        setenv("ISS_TROPHIES_PER_SAVE","1",1);
#endif
    }
    uint32_t flags[4]={},count=99;assert(backend->state(flags,&count)==0);
    if(actualWorld||missingWorld){
        std::ifstream f(argv[4],std::ios::binary);assert(f);std::string bytes((std::istreambuf_iterator<char>(f)),{});assert(bytes.size()==131072);
        backend->save_open(3,0,bytes.size());backend->save_part(3,bytes.data(),0,bytes.size(),0);backend->save_close(3);
        assert(backend->state(flags,&count)==0&&count==48);
        int earned=0;for(auto value:flags)earned+=std::popcount(value);
        assert(earned==(actualWorld?15:7));
        assert((flags[0]&(1u<<25))==(actualWorld?(1u<<25):0));
    }else if(mode=="write"){
        assert(count==48&&!flags[0]&&!flags[1]);int32_t platinum=99;assert(backend->unlock(17,&platinum)==0&&platinum==-1);
        auto path=std::filesystem::path(user)/"trophies/CUSA00223/earned.json";
        auto before=std::filesystem::last_write_time(path);assert(backend->unlock(17,nullptr)==0);assert(std::filesystem::last_write_time(path)==before);
        assert(backend->unlock(-1,nullptr)!=0&&backend->unlock(48,nullptr)!=0);
        assert(backend->state(flags,&count)==0&&count==48&&flags[0]==(1u<<17));
        // A blocked temporary file must not claim an unlock or change memory.
        auto temp=path;temp+=".tmp";std::filesystem::create_directory(temp);
        assert(backend->unlock(18,nullptr)!=0);assert(backend->state(flags,&count)==0&&count==48&&flags[0]==(1u<<17));
        std::filesystem::remove(temp);
    }else{assert(mode=="restart"&&count==48&&flags[0]==(1u<<17)&&!flags[1]&&!flags[2]&&!flags[3]);}
    std::cout<<"Full backend "<<mode<<": archive, API flags, durable unlock and rollback on storage failure passed\n";
}
