#include "PhysicalInteractions.h"
#include "AimTakeover.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "InputPost.h"
#include "SceneQuery.h"
#include "WeaponAttachment.h"
#include "Haptics.h"
#include "MinHookInit.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/PhysicalInteractions.h"
#include "preyvr/PhysicalNative.h"
#include "preyvr/StereoCamera.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>

namespace preyvr::dll {
namespace {
using namespace physical;
std::atomic<unsigned> melee{0},contacts{0},speed{120},strength{50};
std::atomic<std::uint64_t> settings{1},strikes{0},nudges{0},substitutions{0},suppressed{0},refusals{0};
std::atomic<std::uint64_t> publications{0},sweeps{0},found{0},admissionRefused{0};
std::atomic<DWORD> error{0};std::atomic<bool> fault{false},installed{false};
struct Geometry {Sample sample{};RigIdentity rig{};std::uint64_t published=0,settings=0;bool wrench=false;};
LatestSnapshot<Geometry> geometry;
std::mutex publicationMutex;
Swing swing; // input-drain thread only, including the native callbacks below
HitOwnership ownership;
std::uint64_t seenSettings=0;
std::uintptr_t module=0;
struct Vector {scene::Hit* begin=nullptr;scene::Hit* end=nullptr;scene::Hit* capacity=nullptr;};
using Attack=bool(__fastcall*)(std::uintptr_t,unsigned,const void*,int,float);
using Hit=void(__fastcall*)(std::uintptr_t,float);
using GetHits=Vector*(__fastcall*)(std::uintptr_t,Vector*,std::uintptr_t,float);
using WeaponImpulse=void(__fastcall*)(void*,void*,const Vec3*,int,std::uintptr_t,float,float,float);
std::atomic<Attack> originalAttack{nullptr};std::atomic<Hit> originalHit{nullptr};
std::atomic<GetHits> originalGetHits{nullptr};std::atomic<WeaponImpulse> originalImpulse{nullptr};
struct Request {std::uintptr_t weapon=0;scene::Hit hit{};Vec3 direction{};};
thread_local Request request;
bool EngineThread(){return InputDrainThreadId()!=0&&InputDrainThreadId()==GetCurrentThreadId();}
bool __fastcall AttackHook(std::uintptr_t w,unsigned id,const void* name,int activation,float value){
    if(ownership.Suppress(w)&&ownership.BlockAttack(w,*reinterpret_cast<int*>(w+0x520),activation))return false;
    return originalAttack.load()(w,id,name,activation,value);
}
void __fastcall HitHook(std::uintptr_t w,float angle){
    // Animation events may arrive on another native thread. Suppression is
    // atomic and independent of input-drain thread identity or option state.
    if(request.weapon!=w&&ownership.Suppress(w)){++suppressed;return;}
    originalHit.load()(w,angle);
}
Vector* __fastcall GetHitsHook(std::uintptr_t component,Vector* out,std::uintptr_t w,float angle){
    if(EngineThread()&&request.weapon==w&&w&&component==w+0x4c8){
        *out={};
        // The consumer frees the vector. Allocate through its own append,
        // never return stack storage or memory from the mod's CRT heap.
        reinterpret_cast<void(__fastcall*)(Vector*,const scene::Hit*)>(module+native::AppendHit)(out,&request.hit);
        ++substitutions;return out;
    }
    return originalGetHits.load()(component,out,w,angle);
}
void __fastcall ImpulseHook(void* entity,void* collider,const Vec3* direction,int part,std::uintptr_t w,float force,float minMass,float maxMass){
    // Native wrapper negates its direction. Retain native force/stats and
    // material response, replacing only the body-facing direction during our hit.
    const Vec3 incoming=Scale(request.direction,-1);
    originalImpulse.load()(entity,collider,EngineThread()&&request.weapon==w&&w?&incoming:direction,part,w,force,minMass,maxMass);
}
bool Gate(){
    if(module)return true;
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if(!base)return false;
    for(const auto& c:native::Contracts){
        std::uint64_t h=14695981039346656037ull;
        for(std::size_t i=0;i<c.size;++i)h=(h^reinterpret_cast<const unsigned char*>(base+c.rva)[i])*1099511628211ull;
        if(h!=c.hash){error=ERROR_BAD_EXE_FORMAT;fault=true;return false;}
    }
    if(*reinterpret_cast<std::uintptr_t*>(base+native::RigidVtable+0x38)!=base+native::RigidAction||
       *reinterpret_cast<std::uintptr_t*>(base+native::RigidVtable+8)!=base+native::RigidGetType||
       *reinterpret_cast<std::uintptr_t*>(base+0x1d22200+0x98)!=base+native::CharacterAabb){error=ERROR_INVALID_ADDRESS;fault=true;return false;}
    // QueueAction indexes these runtime-initialised descriptor tables by type.
    // Type 2 must copy exactly our 56 bytes and have no pointed-to payloads.
    if(*reinterpret_cast<const unsigned*>(base+0x2982ca8)!=sizeof(Impulse)||
       *reinterpret_cast<const std::uintptr_t*>(base+0x2982df0)!=0){error=ERROR_INVALID_DATA;fault=true;return false;}
    module=base;return true;
}
bool Install(){
    if(installed.load())return true;
    if(!EnsureMinHook())return false;
    void* targets[]={reinterpret_cast<void*>(module+native::Attack),reinterpret_cast<void*>(module+native::WeaponHit),
        reinterpret_cast<void*>(module+native::GetHits),reinterpret_cast<void*>(module+native::WeaponImpulse)};
    void* hooks[]={reinterpret_cast<void*>(AttackHook),reinterpret_cast<void*>(HitHook),reinterpret_cast<void*>(GetHitsHook),reinterpret_cast<void*>(ImpulseHook)};
    void* originals[4]{};unsigned made=0,enabled=0;
    for(;made<4;++made)if(MH_CreateHook(targets[made],hooks[made],&originals[made])!=MH_OK)break;
    if(made==4){
        originalAttack=reinterpret_cast<Attack>(originals[0]);originalHit=reinterpret_cast<Hit>(originals[1]);
        originalGetHits=reinterpret_cast<GetHits>(originals[2]);originalImpulse=reinterpret_cast<WeaponImpulse>(originals[3]);
        for(;enabled<4;++enabled)if(MH_EnableHook(targets[enabled])!=MH_OK)break;
    }
    if(enabled!=4){
        for(unsigned i=0;i<enabled;++i)MH_DisableHook(targets[i]);
        for(unsigned i=0;i<made;++i)MH_RemoveHook(targets[i]);
        error=ERROR_INVALID_FUNCTION;fault=true;return false;
    }
    installed=true;return true;
}
bool Strike(std::uintptr_t weapon,const Contact& c){
    if(*reinterpret_cast<std::uintptr_t*>(weapon)!=module+native::WrenchVtable||
       *reinterpret_cast<int*>(weapon+0x520)!=0||!installed.load())return false;
    // R8/name, EDX/entity and the value argument are not consumed by this
    // concrete implementation. Supply a stable empty name object; do not
    // manufacture a CryName hash. State transition, NOT bool return, admits it.
    static const std::uintptr_t emptyName=0;
    // Also cover an animation callback dispatched synchronously by PlayAnim.
    if(!ownership.Claim(weapon)){++admissionRefused;return false;}
    originalAttack.load()(weapon,0,&emptyName,1,1.f);
    const int state=*reinterpret_cast<int*>(weapon+0x520);
    if(state!=1&&state!=2){ownership.Release(weapon);++admissionRefused;return false;}
    request={weapon,c.hit,c.direction};request.hit.next=nullptr;
    originalHit.load()(weapon,0.f);request={};++strikes;return true;
}
bool NudgeRigid(const Contact& c){
    const auto receiver=reinterpret_cast<std::uintptr_t>(c.hit.collider);
    if(!receiver||*reinterpret_cast<const std::uintptr_t*>(receiver)!=module+native::RigidVtable)return false;
    auto impulse=Nudge(c,strength.load());
    RigidResponse body{};
    // These exact fields are consumed by CRigidEntity::Action(type=2):
    // impulse * inverseMass, and inverseInertia * ((point-centre) x impulse).
    body.inverseMass=*reinterpret_cast<const float*>(receiver+0x2e8);
    std::memcpy(&body.centre,reinterpret_cast<const void*>(receiver+0x298),sizeof(body.centre));
    std::memcpy(body.inverseInertia.data(),reinterpret_cast<const void*>(receiver+0x328),sizeof(body.inverseInertia));
    if(!LimitNudge(impulse,body))return false;
    // bThreadSafe=1 follows the native producer; the engine copies queued
    // actions. Only this verified concrete rigid class is supported.
    const int result=reinterpret_cast<int(__fastcall*)(std::uintptr_t,const Impulse*,int)>(module+native::RigidAction)(receiver,&impulse,1);
    if(result)++nudges;return result!=0;
}
void Act(const Geometry& g,const Motion& motion,const Contact& c){
    __try {
        if(!Gate())return;
        if(g.wrench&&melee.load()){
            if(motion.strike){swing.Contact();Strike(g.rig.weapon,c);}
            // Never add a second impulse on top of the wrench's native hit.
        }else if(contacts.load()&&NudgeRigid(c))swing.Contact();
    }__except(EXCEPTION_EXECUTE_HANDLER){request={};fault=true;error=GetExceptionCode();++refusals;}
}
bool Prepare(bool wrench){
    __try{return Gate()&&(!wrench||!melee.load()||Install());}
    __except(EXCEPTION_EXECUTE_HANDLER){fault=true;error=GetExceptionCode();return false;}
}
}
void PublishPhysicalWeapon(const GameplayPoseFrame& frame,const RigIdentity& rig,const animik::Location& location,
                           Pose wrist,const weaponrig::ContactGeometry& shape){
    if(!PhysicalInteractionsEnabled()||!frame.cameraCentreValid||!frame.headYawUsable||
       !std::isfinite(frame.yaw)||(frame.referenceGeneration&1)||
       !IsPoseUsable(frame.tracking.head,frame.tracking.headValidity,100000000)||
       !IsPoseUsable(frame.tracking.hands[1].gripPose,frame.tracking.hands[1].gripValidity,100000000)||
       frame.tracking.displayTime<=0||!std::isfinite(location.s)||location.s<=0||location.s>2)return;
    const Pose worldFromReference{{0,0,std::sin(frame.yaw*.5f),std::cos(frame.yaw*.5f)}, {}};
    Geometry g{};g.rig=rig;g.wrench=shape.wrench;g.settings=settings.load();
    auto& s=g.sample;s.worldFromReference=worldFromReference;
    s.worldFromReference.position=Sub(frame.cameraCentre,Rotate(worldFromReference.orientation,stereo::ToEngineSpace(frame.tracking.head.position)));
    const auto referenceFromWorld=Inverse(s.worldFromReference);
    const Pose model=Compose(wrist,shape.weaponInWrist);
    const Pose world{Multiply(location.q,model.orientation),animik::ModelToWorld(location,model.position)};
    const auto local=Bounds(shape.minimum,shape.maximum);
    for(unsigned i=0;i<local.size();++i)s.points[i]=Transform(referenceFromWorld,Transform(world,Scale(local[i],location.s)));
    s.grip=stereo::ToEngineSpace(frame.tracking.hands[1].gripPose);
    s.sequence=frame.tracking.sequence;s.stamp=static_cast<std::uint64_t>(frame.tracking.displayTime);
    s.owner=rig.generation;s.epoch=frame.tracking.epoch;s.reference=frame.referenceGeneration;
    g.published=frame.tracking.publishedNs;
    std::lock_guard lock(publicationMutex);
    Geometry previous{};
    if(geometry.TryRead(previous)&&previous.sample.owner==s.owner&&previous.sample.epoch==s.epoch&&
       previous.sample.reference==s.reference&&previous.settings==g.settings&&previous.sample.sequence>=s.sequence)return;
    geometry.Publish(g);++publications;
}
void UpdatePhysicalInteractionsImpl(const GameplayPoseFrame& frame,bool allowed){
    if(!EngineThread())return;
    Geometry g{};EquippedRig owner{};
    const auto generation=settings.load();
    if(generation!=seenSettings){swing.Reset();seenSettings=generation;}
    if(!allowed||!PhysicalInteractionsEnabled()||!HudGameplayInputAllowed()||!frame.cameraCentreValid||!frame.headYawUsable||
       !FreshSample(MonotonicNanoseconds(),frame.tracking.publishedNs,100000000)||
       !IsPoseUsable(frame.tracking.head,frame.tracking.headValidity,100000000)||
       !IsPoseUsable(frame.tracking.hands[1].gripPose,frame.tracking.hands[1].gripValidity,100000000)||
       !geometry.TryRead(g)||!FreshSample(MonotonicNanoseconds(),g.published,100000000)||g.settings!=generation||
       g.sample.epoch!=frame.tracking.epoch||g.sample.reference!=frame.referenceGeneration||
       frame.referenceGeneration!=HeadTrackingReferenceGeneration()||!TryGetEquippedRig(frame.player,owner)||
       owner.generation!=g.rig.generation||!SameRigBinding(owner,g.rig,owner.itemId)) {swing.Reset();return;}
    const auto motion=swing.Update(g.sample,speed.load()*.01f);
    if(!motion.query||(!contacts.load()&&(!g.wrench||!motion.strike)))return;
    // Hook installation can suspend threads. Do it BEFORE obtaining any native
    // collider pointer; a contact never survives an install or a frame boundary.
    if(!Prepare(g.wrench)){++refusals;swing.Reset();return;}
    ++sweeps;
    Contact c{};
    if(!Sweep(motion,[&](Vec3 origin,Vec3 delta,scene::Hit& hit){return QueryPhysicalSegment(frame,origin,delta,hit);},c)){
        ++refusals;swing.Reset();return;
    }
    if(!c.found)return;
    ++found;
    EquippedRig current{};
    if(!HudGameplayInputAllowed()||!TryGetEquippedRig(frame.player,current)||current.generation!=g.rig.generation||
       !SameRigBinding(current,g.rig,current.itemId)||HeadTrackingReferenceGeneration()!=frame.referenceGeneration){++refusals;swing.Reset();return;}
    const auto before=strikes.load()+nudges.load();Act(g,motion,c);
    if(strikes.load()+nudges.load()!=before)QueueHaptic(Hand::right,haptics::Event::WeaponContact,frame.tracking);
}
void UpdatePhysicalInteractions(const GameplayPoseFrame& frame,bool allowed){
    thread_local bool busy=false;
    if(busy)return;
    struct Guard{bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(busy);
    UpdatePhysicalInteractionsImpl(frame,allowed);
}
bool PhysicalInteractionsEnabled(){return !fault.load()&&(melee.load()||contacts.load());}
void SetPhysicalMelee(unsigned v){melee=v!=0;++settings;}unsigned PhysicalMeleeEnabled(){return melee.load();}
void SetPhysicalContacts(unsigned v){contacts=v!=0;++settings;}unsigned PhysicalContactsEnabled(){return contacts.load();}
void SetSwingSpeed(unsigned v){speed=std::clamp(v,80u,250u);++settings;}unsigned SwingSpeed(){return speed.load();}
void SetContactStrength(unsigned v){strength=std::min(v,100u);++settings;}unsigned ContactStrength(){return strength.load();}
std::string PhysicalInteractionsReport(){return " melee="+std::to_string(melee.load())+" contacts="+std::to_string(contacts.load())+
    " speedCms="+std::to_string(speed.load())+" strength="+std::to_string(strength.load())+" hooks="+std::to_string(installed.load())+
    " fault="+std::to_string(fault.load())+" strikes="+std::to_string(strikes.load())+" substituted="+std::to_string(substitutions.load())+
    " poses="+std::to_string(publications.load())+" sweeps="+std::to_string(sweeps.load())+" contactsFound="+std::to_string(found.load())+
    " admissionRefused="+std::to_string(admissionRefused.load())+
    " suppressed="+std::to_string(suppressed.load())+" nudges="+std::to_string(nudges.load())+" refused="+std::to_string(refusals.load())+
    " error="+std::to_string(error.load());}
}
