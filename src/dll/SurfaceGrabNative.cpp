#include "SurfaceGrabNative.h"
#include "SurfaceGrabLane.h"
#include "MinHookInit.h"
#include "preyvr/SurfaceGrabNative.h"
#include "preyvr/SurfaceGrabTransaction.h"
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <mutex>
namespace preyvr::dll {
namespace {
namespace n=surfacegrab::native;
std::atomic<std::uintptr_t> module{0};
std::atomic<surfacegrab::MovementCall> original{nullptr};
std::mutex installation;
bool Prefix(void* request,unsigned& type,Vec3& velocity){
    __try {if(!request)return false;std::memcpy(&type,request,4);std::memcpy(&velocity,static_cast<unsigned char*>(request)+4,12);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Changed(surfacegrab::MovementCall fn,std::uintptr_t self,void* request,Vec3 velocity,std::uint64_t token){
    bool success=false;
    __try {surfacegrab::InvokeMovement(fn,self,request,velocity);success=true;}
    __finally {FinishSurfaceGrabVelocity(token,success);}
}
__declspec(noinline) void __fastcall Movement(std::uintptr_t self,void* request){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto fn=original.load();if(!fn)return;
    if(caller==module.load()+n::Caller){
        unsigned type=0;Vec3 velocity{},desired{};
        const auto token=Prefix(request,type,velocity)?ClaimSurfaceGrabVelocity(self,n::Caller,type,velocity,desired):0;
        if(token){Changed(fn,self,request,desired,token);return;}
    }
    fn(self,request);
}
bool Gate(std::uintptr_t base){
    for(const auto& c:n::Contracts){
        const auto* bytes=reinterpret_cast<const unsigned char*>(base+c.rva);std::uint64_t hash=14695981039346656037ull;
        for(std::size_t k=0;k<c.size;++k)hash=(hash^bytes[k])*1099511628211ull;
        if(hash!=c.hash)return false;
    }
    return *reinterpret_cast<const std::uintptr_t*>(base+n::AnimatedVtable+0x130)==base+n::AddMovement;
}
}
DWORD EnsureSurfaceGrabHook(){
    std::lock_guard lock(installation);if(original.load())return 0;
    const auto flight=EnsureNativeFlightHook();if(flight)return flight;
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if(!base||!Gate(base))return ERROR_REVISION_MISMATCH;
    EnsureMinHook();surfacegrab::MovementCall fn=nullptr;
    auto* target=reinterpret_cast<void*>(base+n::AddMovement);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&Movement),reinterpret_cast<void**>(&fn))!=MH_OK)return ERROR_FUNCTION_FAILED;
    module=base;original=fn;
    if(MH_EnableHook(target)!=MH_OK){original=nullptr;module=0;MH_RemoveHook(target);return ERROR_FUNCTION_FAILED;}
    return 0;
}
DWORD ReadSurfaceGrabOwner(SurfaceOwner& out){
    __try {
        const auto base=module.load();if(!base)return ERROR_NOT_READY;
        const auto error=ReadNativeFlightOwner(out.flight);if(error)return error;
        out.animated=*reinterpret_cast<const std::uintptr_t*>(out.flight.player+0x1808);
        if(!out.animated||*reinterpret_cast<const std::uintptr_t*>(out.animated)!=base+n::AnimatedVtable||
           *reinterpret_cast<const std::uintptr_t*>(out.animated+0x38)!=out.flight.entity)return ERROR_INVALID_ADDRESS;
        // Follow the very same restrictions the native zero-g producer uses.
        if(*reinterpret_cast<const unsigned char*>(out.flight.player+0x58+0x613))return ERROR_ACCESS_DISABLED_BY_POLICY;
        const auto binds=*reinterpret_cast<const std::uintptr_t*>(out.flight.entity+0xA0);
        if(binds&&*reinterpret_cast<const std::uintptr_t*>(binds+0x18))return ERROR_ACCESS_DISABLED_BY_POLICY;
        const auto status=reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t)>(base+0x1587CA0)(out.flight.player+0x678);
        if(!status)return ERROR_NOT_READY;
        if(reinterpret_cast<bool(__fastcall*)(std::uintptr_t,int)>(base+0x148F9B0)(status,6))return ERROR_ACCESS_DISABLED_BY_POLICY;
        return 0;
    }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
bool IsFixedSurfaceCollider(std::uintptr_t collider){
    __try {
        const auto base=module.load();return base&&collider&&
            *reinterpret_cast<const std::uintptr_t*>(collider)==base+n::StaticVtable&&
            *reinterpret_cast<const int*>(collider+0x28)==0;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
