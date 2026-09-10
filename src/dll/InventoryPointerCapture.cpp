#include "InventoryPointerCapture.h"
#include "Bootstrap.h"
#include "HudBridge.h"
#include "Logger.h"
#include "MinHookInit.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace preyvr::dll {
namespace {
// Steam constructor 0x162B010 registers inventoryPickItem -> 0x162CFE0.
// OnPlaceItem 0x162D330 calls CancelPickItem 0x162B840 on failed placement.
using PickFn=void(__fastcall*)(void*,void*,const void*,const void*);
using CancelFn=bool(__fastcall*)(void*);
constexpr std::uintptr_t kPick=0x162CFE0,kCancel=0x162B840,kInventoryTable=0x1E64B40;
constexpr std::array<unsigned char,23> kPickHead{
    0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57,
    0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xc9};
constexpr std::array<unsigned char,23> kCancelHead{
    0x40,0x53,0x56,0x57,0x41,0x54,0x48,0x81,0xec,0x88,0,0,0,
    0x8b,0x71,0x28,0x45,0x32,0xe4,0x83,0x79,0x30,0};
std::mutex gInstallMutex;
std::atomic<PickFn> gPickOriginal{nullptr};
bool gCreated=false,gInstalled=false;
thread_local bool gPointerHeld=false;
struct Capture {std::uintptr_t receiver=0,sender=0;unsigned item=0;DWORD thread=0;};
thread_local Capture gCapture;

bool CodeMatches(std::uintptr_t base,bool includePick) {
    __try {
        return (!includePick||std::memcmp(reinterpret_cast<void*>(base+kPick),kPickHead.data(),kPickHead.size())==0)&&
            std::memcmp(reinterpret_cast<void*>(base+kCancel),kCancelHead.data(),kCancelHead.size())==0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void ObservePick(void* self,void* sender) {
    // Flash queues mouse input; OnPick runs during a later Advance, outside
    // HudDispatchPointer. The ownership window is our outstanding button down.
    if(!gPointerHeld)return;
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    __try {
        const auto p=reinterpret_cast<std::uintptr_t>(self);
        if(base&&*reinterpret_cast<std::uintptr_t*>(p)==base+kInventoryTable&&
            *reinterpret_cast<unsigned char*>(p+0x34)==1&&*reinterpret_cast<unsigned char*>(p+0x35)==1) {
            gCapture={p,reinterpret_cast<std::uintptr_t>(sender),*reinterpret_cast<unsigned*>(p+0x28),GetCurrentThreadId()};
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){gCapture={};}
}
void __fastcall PickWithCapture(void* self,void* sender,const void* event,const void* args) {
    const auto original=gPickOriginal.load();
    if(!original)return;
    original(self,sender,event,args);
    ObservePick(self,sender);
}
DWORD CancelChecked(std::uintptr_t base,const Capture& c,bool* cancelled) {
    __try {
        if(*reinterpret_cast<std::uintptr_t*>(c.receiver)!=base+kInventoryTable||
           *reinterpret_cast<std::uintptr_t*>(c.sender)!=base+0x1CAB358)return ERROR_INVALID_ADDRESS;
        // IsVisible 0x2FD150 reads sender+0x70. Native close owns hidden UI cleanup.
        if(!*reinterpret_cast<unsigned char*>(c.sender+0x70))return 0;
        if(*reinterpret_cast<unsigned*>(c.receiver+0x28)!=c.item||
           !*reinterpret_cast<unsigned char*>(c.receiver+0x34)||
           !*reinterpret_cast<unsigned char*>(c.receiver+0x35))return 0;
        reinterpret_cast<CancelFn>(base+kCancel)(reinterpret_cast<void*>(c.receiver));
        *cancelled=true;
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
}
DWORD EnsureInventoryPointerCapture() {
    std::lock_guard lock(gInstallMutex);
    if(gInstalled)return 0;
    if(!ModulePinStatus())return ERROR_INVALID_STATE;
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if(!base||!CodeMatches(base,!gCreated))return ERROR_BAD_EXE_FORMAT;
    if(!EnsureMinHook())return ERROR_NOT_READY;
    auto* target=reinterpret_cast<void*>(base+kPick);
    if(!gCreated) {
        void* original=nullptr;
        if(MH_CreateHook(target,reinterpret_cast<void*>(&PickWithCapture),&original)!=MH_OK)return ERROR_INVALID_FUNCTION;
        gPickOriginal.store(reinterpret_cast<PickFn>(original));gCreated=true;
    }
    const auto result=MH_EnableHook(target);
    if(result!=MH_OK&&result!=MH_ERROR_ENABLED)return ERROR_INVALID_FUNCTION;
    gInstalled=true;
    lifecycle::Log("preyvr_pointer inventory_capture_hook=installed");
    return 0;
}
void BeginInventoryPointerEvent(int event) {
    if(event==1){gCapture={};gPointerHeld=true;}
}
void EndInventoryPointerEvent(int event) {
    if(event==2){gCapture={};gPointerHeld=false;}
}
DWORD CancelInventoryPointerCapture(bool* cancelled) {
    if(!cancelled)return ERROR_INVALID_PARAMETER;
    *cancelled=false;
    const auto c=gCapture;gCapture={};
    if(!c.receiver||!c.sender||!c.item||c.thread!=GetCurrentThreadId()||!HudMenuIsOpen())return 0;
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if(!base||!CodeMatches(base,false))return ERROR_BAD_EXE_FORMAT;
    const auto result=CancelChecked(base,c,cancelled);
    if(*cancelled)lifecycle::Log("preyvr_pointer inventory_drag_cancelled=1");
    return result;
}
}
