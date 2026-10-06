#include "SceneQuery.h"
#include "AimTakeover.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "InputPost.h"
#include "preyvr/SceneQuery.h"
#include "preyvr/SceneQueryNative.h"
#include "preyvr/LatestSnapshot.h"
#include <atomic>
namespace preyvr::dll {
namespace {
std::atomic<bool> enabled{false},fault{false};
std::atomic<unsigned long long> hits{0},misses{0},refused{0};
std::atomic<DWORD> lastError{0};
bool Gate(std::uintptr_t base){
    for(const auto& c:scene::native::Contracts){
        const auto bytes=reinterpret_cast<const unsigned char*>(base+c.rva);
        std::uint64_t hash=14695981039346656037ull;
        for(std::size_t n=0;n<c.size;++n)hash=(hash^bytes[n])*1099511628211ull;
        if(hash!=c.hash)return false;
    }
    return true;
}
DWORD Query(const GameplayPoseFrame& frame,Vec3 origin,Vec3 direction,float range,scene::Hit& hit){
    hit={};hit.distance=-1;
    __try {
        if(InputDrainThreadId()==0||InputDrainThreadId()!=GetCurrentThreadId())return ERROR_INVALID_THREAD_ID;
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
        if(!base||!frame.player)return ERROR_NOT_READY;
        static bool gated=false;
        if(!gated){if(!Gate(base))return ERROR_BAD_EXE_FORMAT;gated=true;}
        const auto player=reinterpret_cast<std::uintptr_t(__fastcall*)()>(base+0x157c990)();
        if(player!=frame.player)return ERROR_NOT_READY;
        const auto entity=*reinterpret_cast<const std::uintptr_t*>(player+scene::native::PlayerEntityOffset);
        if(!entity)return ERROR_NOT_READY;
        const auto entityTable=*reinterpret_cast<const std::uintptr_t*>(entity);
        if(!entityTable||*reinterpret_cast<const std::uintptr_t*>(entityTable+scene::native::PhysicsSlot)!=base+scene::native::EntityPhysics)
            return ERROR_INVALID_ADDRESS;
        void* skip[1]={reinterpret_cast<void*(__fastcall*)(std::uintptr_t)>(base+scene::native::EntityPhysics)(entity)};
        if(!skip[0])return ERROR_NOT_READY;
        const auto world=*reinterpret_cast<const std::uintptr_t*>(base+scene::native::WorldPointer);
        if(!world)return ERROR_NOT_READY;
        const auto table=*reinterpret_cast<const std::uintptr_t*>(world);
        if(table!=base+scene::native::WorldVtable||*reinterpret_cast<const std::uintptr_t*>(table+0x118)!=base+scene::native::Query)
            return ERROR_INVALID_ADDRESS;
        scene::Params params{};
        if(!scene::Build(params,origin,direction,hit,skip,1,range))return ERROR_INVALID_DATA;
        // Synchronous flags only. Stack skip/hit storage cannot escape. Native
        // caller=4 acquires the physics caller lock; never call from render/XR.
        using Ray=int(__fastcall*)(std::uintptr_t,const scene::Params*,const char*,int);
        const int count=reinterpret_cast<Ray>(base+scene::native::Query)(world,&params,"RayWorldIntersection(PreyVR)",4);
        if(count==0){hit={};hit.distance=-1;++misses;return ERROR_SUCCESS;}
        if(scene::Distance(count,hit)<0||hit.distance>range+.001f)return ERROR_INVALID_DATA;
        ++hits;return ERROR_SUCCESS;
    }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
}
void QueryAimScene(const GameplayPoseFrame& frame,aim::Sample& sample){
    sample.sceneDistance=-1;
    if(!enabled.load()||fault.load())return;
    if(!HudGameplayInputAllowed()||!FreshSample(MonotonicNanoseconds(),frame.tracking.publishedNs)||
       frame.referenceGeneration!=HeadTrackingReferenceGeneration()){++refused;return;}
    thread_local bool busy=false;
    if(busy){++refused;return;}
    scene::Hit hit{};
    busy=true;const auto result=Query(frame,sample.origin,sample.direction,scene::Range,hit);busy=false;
    sample.sceneDistance=hit.distance;
    lastError=result;
    if(result){sample.sceneDistance=-1;++refused;
        // Transient lack of player/physics or wrong thread can recover. Bad ABI,
        // malformed output or access faults remain disarmed for this process.
        if(result!=ERROR_NOT_READY&&result!=ERROR_INVALID_THREAD_ID)fault=true;
    }
}
bool QueryPhysicalSegment(const GameplayPoseFrame& frame,Vec3 origin,Vec3 delta,scene::Hit& hit){
    hit={};hit.distance=-1;
    const float length=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
    if(fault.load()||!std::isfinite(length)||length<.001f||length>.501f||!HudGameplayInputAllowed()||
       !FreshSample(MonotonicNanoseconds(),frame.tracking.publishedNs,100000000)||
       frame.referenceGeneration!=HeadTrackingReferenceGeneration())return false;
    const auto result=Query(frame,origin,{delta.x/length,delta.y/length,delta.z/length},length,hit);
    if(result){lastError=result;++refused;
        if(result!=ERROR_NOT_READY&&result!=ERROR_INVALID_THREAD_ID)fault=true;
        hit={};hit.distance=-1;
    }
    return result==ERROR_SUCCESS;
}
bool QueryDebugRay(const GameplayPoseFrame& frame,Vec3 origin,Vec3 direction,scene::Hit& hit){
    hit={};hit.distance=-1;
    if(fault.load()||!HudGameplayInputAllowed()||!FreshSample(MonotonicNanoseconds(),frame.tracking.publishedNs)||
       frame.referenceGeneration!=HeadTrackingReferenceGeneration())return false;
    thread_local bool busy=false;
    if(busy)return false;
    busy=true;const auto result=Query(frame,origin,direction,scene::Range,hit);busy=false;
    if(result){lastError=result;++refused;
        if(result!=ERROR_NOT_READY&&result!=ERROR_INVALID_THREAD_ID)fault=true;
        hit={};hit.distance=-1;
    }
    return result==ERROR_SUCCESS;
}
bool QuerySegment(const GameplayPoseFrame& frame,Vec3 a,Vec3 b,scene::Hit& hit){
    hit={};hit.distance=-1;
    const Vec3 d{b.x-a.x,b.y-a.y,b.z-a.z};
    const float length=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
    if(fault.load()||!std::isfinite(length)||length<.001f||length>2.f||!HudGameplayInputAllowed()||
       !FreshSample(MonotonicNanoseconds(),frame.tracking.publishedNs)||
       frame.referenceGeneration!=HeadTrackingReferenceGeneration())return false;
    thread_local bool busy=false;
    if(busy)return false;
    busy=true;const auto result=Query(frame,a,{d.x/length,d.y/length,d.z/length},length,hit);busy=false;
    if(result){lastError=result;++refused;
        if(result!=ERROR_NOT_READY&&result!=ERROR_INVALID_THREAD_ID)fault=true;
        hit={};hit.distance=-1;
    }
    return result==ERROR_SUCCESS;
}
bool SceneQueryFaulted(){return fault.load();}
void SetSceneReticle(unsigned on){enabled=on!=0;}
unsigned SceneReticleEnabled(){return enabled.load()?1u:0u;}
std::string SceneQueryReport(){return " enabled="+std::to_string(enabled.load())+" fault="+std::to_string(fault.load())+
    " hits="+std::to_string(hits.load())+" misses="+std::to_string(misses.load())+
    " refused="+std::to_string(refused.load())+" error="+std::to_string(lastError.load());}
}
