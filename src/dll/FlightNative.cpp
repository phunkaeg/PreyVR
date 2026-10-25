#include "FlightNative.h"
#include "MinHookInit.h"
#include "InputPost.h"
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <mutex>
#include <cstring>

namespace preyvr::dll {
namespace {
using RotationFn=flight::NativeQuat*(__fastcall*)(std::uintptr_t,flight::NativeQuat*);
std::atomic<RotationFn> original{nullptr};
std::atomic<std::uintptr_t> moduleBase{0};
std::mutex installMutex;
// Steam byte/constructor/vtable witnesses: tools/re/verify_native_flight.py.
constexpr std::uintptr_t Getter=0x904D30,EntityVtable=0x1D30BD0,GetPlayer=0x157C990;
constexpr std::uintptr_t Sprint=0x1591120;
__declspec(noinline) flight::NativeQuat* __fastcall Rotation(std::uintptr_t entity,flight::NativeQuat* out) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto fn=original.load();
    auto* result=fn?fn(entity,out):nullptr;
    if(result&&result==out&&caller==moduleBase.load()+0x1270651)
        ApplyNativeFlightRotation(entity,0x1270651,*out);
    return result;
}
bool Matches(std::uintptr_t base,std::uintptr_t rva,const unsigned char* bytes,std::size_t size) {
    return std::memcmp(reinterpret_cast<const void*>(base+rva),bytes,size)==0;
}
bool Gates(std::uintptr_t base) {
    const unsigned char getter[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x55,0x48,0x8d,0x68,0xa1,0x48,0x81,0xec,0xd0,0,0,0};
    const unsigned char site[]={0x49,0x8b,0x07,0x48,0x8d,0x54,0x24,0x58,0x49,0x8b,0xcf,0xff,0x90,0x50,0x01,0,0};
    const unsigned char state[]={0xc7,0x06,0x09,0,0,0};
    // Checked separately because an accepted detour does not validate a setter.
    const unsigned char sprint[]={0x88,0x91,0xac,0,0,0,0x84,0xd2};
    const unsigned char inputBase[]={0x48,0x8d,0x88,0xe0,0x08,0,0,0x48,0x8b,0xd3};
    const unsigned char leanLeft[]={0x0f,0xb6,0x91,0xb5,0,0,0};
    const unsigned char leanRight[]={0x0f,0xb6,0x81,0xb6,0,0,0};
    const unsigned char braking[]={0xc6,0x81,0xb1,0,0,0,1};
    const unsigned char getPlayer[]={0x48,0x83,0xec,0x28,0x48,0x8b,0x0d,0x8d,0x10,0xcd,0,0x48,0x8b,0x01,0xff,0x50,0x70};
    return Matches(base,Getter,getter,sizeof getter)&&Matches(base,0x1270640,site,sizeof site)&&
        Matches(base,0x159C45F,state,sizeof state)&&Matches(base,Sprint+4,sprint,sizeof sprint)&&
        Matches(base,0x159CBB1,inputBase,sizeof inputBase)&&Matches(base,0x15910C2,leanLeft,sizeof leanLeft)&&
        Matches(base,0x15910DD,leanRight,sizeof leanRight)&&Matches(base,0x1591109,braking,sizeof braking)&&
        Matches(base,GetPlayer,getPlayer,sizeof getPlayer)&&
        *reinterpret_cast<const std::uintptr_t*>(base+EntityVtable+0x150)==base+Getter;
}
}
DWORD EnsureNativeFlightHook() {
    std::lock_guard lock(installMutex);
    if(original.load())return 0;
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if(!base)return ERROR_MOD_NOT_FOUND;
    if(!Gates(base))return ERROR_REVISION_MISMATCH;
    EnsureMinHook();RotationFn fn=nullptr;
    const auto target=reinterpret_cast<void*>(base+Getter);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&Rotation),reinterpret_cast<void**>(&fn))!=MH_OK)return ERROR_FUNCTION_FAILED;
    moduleBase=base;original=fn;
    if(MH_EnableHook(target)!=MH_OK){original=nullptr;moduleBase=0;MH_RemoveHook(target);return ERROR_FUNCTION_FAILED;}
    return 0;
}
DWORD ReadNativeFlightOwner(FlightOwner& out) {
    __try {
        const auto base=moduleBase.load();if(!base)return ERROR_NOT_READY;
        out.player=reinterpret_cast<std::uintptr_t(__fastcall*)()>(base+GetPlayer)();
        if(!out.player)return ERROR_NOT_READY;
        out.entity=*reinterpret_cast<const std::uintptr_t*>(out.player+0x38);
        out.input=out.player+0x8E0;
        out.state=*reinterpret_cast<const int*>(out.player+0x58);
        out.cinematic=*reinterpret_cast<const int*>(out.input+0x94);
        if(!out.entity||*reinterpret_cast<const std::uintptr_t*>(out.entity)!=base+EntityVtable||
           *reinterpret_cast<const std::uintptr_t*>(out.input+0x70)!=out.player)return ERROR_INVALID_ADDRESS;
        return 0;
    }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
DWORD SetNativeFlightBoost(std::uintptr_t player,bool pressed) {
    __try {
        if(!InputDrainThreadId()||InputDrainThreadId()!=GetCurrentThreadId())return ERROR_INVALID_THREAD_ID;
        FlightOwner owner{};const auto error=ReadNativeFlightOwner(owner);if(error)return error;
        // A replacement player never inherits a boost or receives a write to
        // the old player's input object. There is nothing to release in it.
        if(owner.player!=player)return pressed?ERROR_NOT_READY:0;
        if(pressed&&(owner.state!=9||owner.cinematic))return ERROR_NOT_READY;
        reinterpret_cast<void(__fastcall*)(std::uintptr_t,bool)>(moduleBase.load()+Sprint)(owner.input,pressed);
        return 0;
    }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
DWORD SetNativeFlightBrake(std::uintptr_t player,bool pressed) {
    __try {
        if(!InputDrainThreadId()||InputDrainThreadId()!=GetCurrentThreadId())return ERROR_INVALID_THREAD_ID;
        FlightOwner owner{};const auto error=ReadNativeFlightOwner(owner);if(error)return error;
        if(owner.player!=player)return pressed?ERROR_NOT_READY:0;
        if(pressed&&(owner.state!=9||owner.cinematic))return ERROR_NOT_READY;
        // Native OnActionLeanLeft/Right write these adjacent flags and call
        // ProcessLeanInput. Set the pair together: separate shoulder posts can
        // fail halfway, leaving one roll direction held. The native leaf then
        // computes rawLean=0 and zeroGBraking=pressed (cinematics still clear it).
        *reinterpret_cast<volatile unsigned short*>(owner.input+0xB5)=pressed?0x0101:0;
        reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(moduleBase.load()+0x15910B0)(owner.input);
        return 0;
    }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
}
