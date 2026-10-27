// SPDX-License-Identifier: GPL-2.0-or-later
#include "tlg_trophies.h"
#include "tlg_png.h"
#ifndef ISS_TROPHY_BACKEND_TEST
#include "imgui_internal.h"
#endif
#include "../../src/iss_trophies.h"
#include "../../src/trophy_store.h"
#include "../../src/trophy_save_session.h"
#include "../../src/trophy_catalogue.h"
#include <mutex>
#include <fstream>
#include <algorithm>
#include <ctime>
#include <cstring>
extern "C" int runtime_file_translate(const char*,char*,size_t);
extern "C" const char *runtime_file_user_dir(void);
namespace {
constexpr int32_t Invalid=int32_t(0x80551604u),StorageError=int32_t(0x80551607u);
std::mutex lock;
bool attempted=false,ready=false;
bool perSave=false;
bool collectibleRecovery=false;
IssTrophies::SaveSession saveSession;
std::string problem;
std::array<std::vector<unsigned char>,48> pngs;
#ifndef ISS_TROPHY_BACKEND_TEST
std::array<ImTextureData*,48> textures{};
#endif
IssTrophies::Progress progress;
std::filesystem::path ledger;
#ifndef ISS_TROPHY_BACKEND_TEST
bool textureAttempted=false;
#endif
bool VerifyCollectibleData(){
    try{
        auto hash=[](const char* guest,size_t maximum){
            char path[2048];if(runtime_file_translate(guest,path,sizeof(path)))return std::string{};
            std::ifstream file(path,std::ios::binary|std::ios::ate);
            if(!file||file.tellg()<=0||uint64_t(file.tellg())>maximum)return std::string{};
            std::vector<unsigned char> bytes(size_t(file.tellg()));file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
            return file?IssTrophies::Fingerprint(bytes):std::string{};
        };
        return IssTrophies::CollectibleDataMatches(hash("/app0/art/cache/world_seaport.xpps",64*1024*1024),hash("/app0/art/cache/i3.sprig.xpps",4*1024*1024));
    }catch(const std::exception&){return false;}
}
void Init(){
    if(attempted)return;attempted=true;
    try{
        char path[2048];
        if(runtime_file_translate("/app0/sce_sys/trophy/trophy00.trp",path,sizeof(path)))throw std::runtime_error("Game trophy archive is unavailable.");
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        if(!file||file.tellg()<=0||file.tellg()>32*1024*1024)throw std::runtime_error("Cannot read game trophy archive.");
        std::vector<unsigned char> bytes(size_t(file.tellg()));file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        if(!file)throw std::runtime_error("Incomplete trophy archive read.");
        auto entries=IssTrophies::ParseTrp(bytes);
        for(auto& entry:entries){
            for(int id=0;id<48;id++){
                char name[32];std::snprintf(name,sizeof(name),"TROP%03d.PNG",id);
                if(entry.name==name&&entry.flags==0){
                    if(entry.data.size()<24||std::memcmp(entry.data.data(),"\x89PNG\r\n\x1a\n",8))throw std::runtime_error("Invalid trophy icon.");
                    pngs[id]=std::move(entry.data);break;
                }
            }
        }
        for(auto& png:pngs)if(png.empty())throw std::runtime_error("Expected Second Son's 48 trophy icons.");
        ledger=std::filesystem::path(runtime_file_user_dir())/"trophies"/"CUSA00223"/"earned.json";
        const char* option=std::getenv("ISS_TROPHIES_PER_SAVE");perSave=option&&!std::strcmp(option,"1");
        if(perSave&&!runtime_trophy_save_mode_ready())throw std::runtime_error("Save-specific trophy game-code check is not ready or did not match.");
        auto fingerprint=IssTrophies::Fingerprint(bytes);
        if(perSave){
            saveSession.Configure(ledger.parent_path(),fingerprint);saveSession.EnableRecovery(true);
            collectibleRecovery=VerifyCollectibleData();saveSession.EnableCollectibleRecovery(collectibleRecovery);
            std::printf("Trophies: collectible dataset verification %s\n",collectibleRecovery?"matched; recovery enabled":"unavailable/mismatched; collectible recovery disabled");
        }
        else progress=IssTrophies::Load(ledger,fingerprint,48);
        ready=true;
        std::printf("Trophies: 48 local game icons loaded; %zu earned. Names remain encrypted.\n",progress.earned.size());
    }catch(const std::exception& e){problem=e.what();std::fprintf(stderr,"Trophies: %s\n",problem.c_str());}
}
int32_t Unlock(int32_t id,int32_t* platinum){
    std::lock_guard guard(lock);Init();if(platinum)*platinum=-1;
    if(!ready||id<0||id>=48)return Invalid;
    if(perSave){
        if(!saveSession.Bound())return Invalid;
        if(saveSession.Current().earned.contains(id))return 0;
        if(!saveSession.Earn(id,int64_t(std::time(nullptr))))return Invalid;
        std::printf("Runtime: trophy %d earned for active save; save gameplay to retain it\n",id);return 0;
    }
    if(progress.earned.contains(id))return 0;
    auto next=progress;next.earned[id]=int64_t(std::time(nullptr));
    try{IssTrophies::Save(ledger,next);progress=std::move(next);problem.clear();}
    catch(const std::exception& e){problem=e.what();std::fprintf(stderr,"Trophies: unlock %d not committed: %s\n",id,e.what());return StorageError;}
    // No inferred platinum or save-completion unlocks: only game-reported IDs.
    std::printf("Runtime: trophy %d unlocked and saved locally\n",id);return 0;
}
int32_t State(uint32_t* flags,uint32_t* count){
    if(!flags||!count)return Invalid;
    std::lock_guard guard(lock);Init();std::fill(flags,flags+4,0);*count=0;
    if(!ready)return Invalid;
    // The API count is the total number of trophies, not the earned count.
    // Second Son uses it as the bound when copying all returned flag bits.
    for(auto [id,time]:(perSave?saveSession.Current():progress).earned)flags[id/32]|=uint32_t(1)<<(id%32);
    *count=48;return 0;
}
void SaveOpen(int fd,int writing,uint64_t length){std::lock_guard guard(lock);Init();if(ready&&perSave)saveSession.Open(fd,writing!=0,length);}
void SavePart(int fd,const void* data,int64_t offset,int64_t size,int writing){std::lock_guard guard(lock);if(ready&&perSave)saveSession.Part(fd,data,offset,size,writing!=0);}
void SaveClose(int fd){
    std::lock_guard guard(lock);if(!ready||!perSave)return;
    try{saveSession.Close(fd);problem=saveSession.RecoveryProblem();if(saveSession.Bound())std::printf("Trophies: active save version %.12s; %zu earned (%zu recovered)\n",saveSession.Current().save_identity.c_str(),saveSession.Current().earned.size(),saveSession.Current().recovered.size());}
    catch(const std::exception& e){problem=e.what();std::fprintf(stderr,"Trophies: save tracking: %s\n",e.what());}
}
void ProgressReset(){
    std::lock_guard guard(lock);Init();if(!ready||!perSave)return;
    saveSession.ProgressReset();problem.clear();std::puts("Trophies: player progression reset; prior save tracking detached");
}
const IssTrophyBackend backend{Unlock,State,SaveOpen,SavePart,SaveClose,ProgressReset};
struct Register { Register(){runtime_trophy_set_backend(&backend);} } registration;
}
#ifndef ISS_TROPHY_BACKEND_TEST
void TlgTrophiesUpdateTextures(){
    {std::lock_guard guard(lock);Init();if(!ready)return;}
    if(!textureAttempted){
        textureAttempted=true;
        for(int id=0;id<48;id++){
            TlgPng::Image image;
            if(!TlgPng::DecodeRgba(pngs[id].data(),pngs[id].size(),image)||image.width<1||image.height<1||image.width>512||image.height>512)continue;
            auto *t=IM_NEW(ImTextureData)();t->Create(ImTextureFormat_RGBA32,image.width,image.height);
            std::memcpy(t->GetPixels(),image.rgba.data(),image.rgba.size());ImGui::RegisterUserTexture(t);textures[id]=t;
        }
    }
}
void TlgTrophiesMenu(){
    std::array<int64_t,48> times{};std::set<int> recovered;std::string error,identity;bool available,bound=false;
    {
        std::lock_guard guard(lock);Init();available=ready;error=problem;
        const auto& shown=perSave?saveSession.Current():progress;
        for(auto [id,time]:shown.earned)times[id]=time;
        recovered=shown.recovered;
        if(perSave){bound=saveSession.Bound();identity=shown.save_identity;}
    }
    ImGui::TextUnformatted("inFAMOUS Second Son");
    if(!available){ImGui::TextWrapped("Trophies unavailable: %s",error.c_str());return;}
    if(perSave){
        if(bound)ImGui::Text("Save version (changes when saved): %.12s",identity.c_str());
        else ImGui::TextWrapped("Load a save or finish creating a new save to begin save-specific tracking.");
    }
    int earned=std::count_if(times.begin(),times.end(),[](int64_t t){return t>0;});
    char label[80];std::snprintf(label,sizeof(label),"%d of 48 trophies earned",earned);
    ImGui::ProgressBar(earned/48.f,ImVec2(-1,0),label);
    const float side=ImGui::GetFontSize()*3.4f;
    int columns=std::max(1,int(ImGui::GetContentRegionAvail().x/(side+ImGui::GetStyle().ItemSpacing.x*2)));
    if(ImGui::BeginTable("trophy_grid",columns,ImGuiTableFlags_SizingStretchSame)){
        for(int id=0;id<48;id++){
            ImGui::TableNextColumn();ImGui::PushID(id);
            ImVec2 pos=ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("trophy",ImVec2(side,side));
            bool hover=ImGui::IsItemHovered(),focus=ImGui::IsItemFocused();
            if(textures[id])ImGui::GetWindowDrawList()->AddImage(textures[id]->GetTexRef(),pos,ImVec2(pos.x+side,pos.y+side),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,times[id]?255:90));
            if(times[id])ImGui::GetWindowDrawList()->AddRect(pos,ImVec2(pos.x+side,pos.y+side),IM_COL32(238,191,76,210),4);
            if(hover||focus){
                ImGui::BeginTooltip();
                const auto* text=IssTrophies::TextForTrophy(id);
                if(text){
                    ImGui::TextUnformatted(text->name.data(),text->name.data()+text->name.size());
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+ImGui::GetFontSize()*28);
                    ImGui::TextUnformatted(text->requirement.data(),text->requirement.data()+text->requirement.size());
                    ImGui::PopTextWrapPos();ImGui::Separator();
                }
                ImGui::Text("Trophy %d: %s",id,times[id]?"earned":"not earned yet");
                if(recovered.contains(id))ImGui::TextUnformatted("Recovered from saved progression; original earning time unavailable.");
                else if(times[id]){
                    auto time=std::time_t(times[id]);std::tm tm{};
#ifdef _WIN32
                    localtime_s(&tm,&time);
#else
                    localtime_r(&time,&tm);
#endif
                    char stamp[64];std::strftime(stamp,sizeof(stamp),"Earned locally: %Y-%m-%d %H:%M",&tm);ImGui::TextUnformatted(stamp);
                }
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }ImGui::EndTable();
    }
    ImGui::Separator();
    ImGui::TextWrapped("Icons come from your game. English names and concise requirements are included locally; hovering or focusing an icon shows details.");
    if(perSave)ImGui::TextWrapped("Save your game to retain new trophies. Recovery supports story decisions/missions (IDs 2-11) and saved event counters (17, 19, 22, 42). Other historical trophies are still under investigation. No PSN sync.");
    if(perSave)ImGui::TextWrapped("%s",collectibleRecovery?"Verified collectible recovery enabled (IDs 25-32).":"Collectible recovery disabled: game world/script dataset did not match. Live trophies and other tracking remain available.");
    else ImGui::TextWrapped("Local progress records trophies reported during play. Existing saves do not automatically import past trophies. No PSN sync.");
    if(!error.empty())ImGui::TextWrapped("Tracking / recovery status: %s",error.c_str());
}
#endif
