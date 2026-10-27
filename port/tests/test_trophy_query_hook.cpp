#undef NDEBUG
#include "../src/trophy_query_hook.h"
#include <vector>
#include <cassert>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif
static int original_calls;
static void *original_object;
static void __attribute__((sysv_abi)) OriginalCall(void* object){++original_calls;original_object=object;}
int main(){
 std::vector<unsigned char> b(ISS_TROPHY_QUERY_SITE+sizeof(iss_trophy_query_signature));
 memcpy(b.data()+ISS_TROPHY_QUERY_SITE,iss_trophy_query_signature,sizeof(iss_trophy_query_signature));
 assert(iss_trophy_query_valid(b.data(),b.size(),"CUSA00223","01.07"));
 assert(!iss_trophy_query_valid(b.data(),b.size(),"CUSA00004","01.07"));
 assert(!iss_trophy_query_valid(b.data(),b.size(),"CUSA00223","01.00"));
 assert(!iss_trophy_query_valid(b.data(),b.size()-1,"CUSA00223","01.07"));
 for(size_t i=0;i<sizeof(iss_trophy_query_signature);i++){
  b[ISS_TROPHY_QUERY_SITE+i]^=1;assert(!iss_trophy_query_valid(b.data(),b.size(),"CUSA00223","01.07"));b[ISS_TROPHY_QUERY_SITE+i]^=1;
 }
 memcpy(b.data()+ISS_TROPHY_RESET_SITE,iss_trophy_reset_signature,sizeof(iss_trophy_reset_signature));
 assert(iss_trophy_reset_valid(b.data(),b.size()));
 for(size_t i=0;i<sizeof(iss_trophy_reset_signature);i++){
  b[ISS_TROPHY_RESET_SITE+i]^=1;assert(!iss_trophy_reset_valid(b.data(),b.size()));b[ISS_TROPHY_RESET_SITE+i]^=1;
 }
#ifdef _WIN32
 // Execute a synthetic original prologue/callee/epilogue to verify forwarding.
 // No game file is mapped or changed; the callee and epilogue are synthetic.
 auto *mem=(unsigned char*)VirtualAlloc(nullptr,0x210000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(mem);
 auto *trampoline=mem+0x200000;assert(iss_trophy_reset_trampoline(trampoline,(uintptr_t)mem)==40);
 auto *callee=mem+ISS_TROPHY_RESET_CALLEE;
 const unsigned char jump[]={0xff,0x25,0,0,0,0};memcpy(callee,jump,6);uintptr_t fn=(uintptr_t)OriginalCall;memcpy(callee+6,&fn,8);
 const unsigned char epilogue[]={0x5b,0x41,0x5e,0x5d,0xc3};memcpy(mem+ISS_TROPHY_RESET_SITE+15,epilogue,sizeof(epilogue));
 DWORD previous;assert(VirtualProtect(mem,0x210000,PAGE_EXECUTE_READ,&previous));FlushInstructionCache(GetCurrentProcess(),mem,0x210000);
 using Reset=void(__attribute__((sysv_abi)) *)(void*);((Reset)trampoline)((void*)0x1234);
 assert(original_calls==1&&original_object==(void*)0x1234);VirtualFree(mem,0,MEM_RELEASE);
#endif
 std::cout<<"Trophy query guards: verified match, all signature-byte mutations, wrong title/version and truncation passed\n";
}
