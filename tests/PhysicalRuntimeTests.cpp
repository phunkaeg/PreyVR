// Production dispatch with native calls replaced at their ABI boundary.
#include "../src/dll/PhysicalInteractions.cpp"
#include <cstdlib>
#include <iostream>
#include <thread>
namespace {
using namespace preyvr;using namespace preyvr::dll;
alignas(16) unsigned char weaponBytes[0x600]{};
EquippedRig currentRig{};bool modal=false,admit=true,queryRefused=false;
unsigned attacks=0,nativeHits=0,appends=0,freed=0,pulses=0,rays=0;
const DWORD drainThread=GetCurrentThreadId();
Vec3 impulseDirection{};physical::Impulse lastImpulse{};
void Check(bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
void __fastcall Append(Vector* out,const scene::Hit* h){
    Check(!out->begin&&!out->end&&!out->capacity,"empty engine vector at append boundary");
    out->begin=new scene::Hit(*h);out->end=out->capacity=out->begin+1;++appends;
}
bool __fastcall NativeAttack(std::uintptr_t w,unsigned,const void*,int activation,float){
    ++attacks;if(admit&&activation==1)*reinterpret_cast<int*>(w+0x520)=1;
    return false; // Native idle attack returns false even when admitted.
}
Vector* __fastcall NativeGetHits(std::uintptr_t,Vector* out,std::uintptr_t,float){*out={};return out;}
void __fastcall NativeHit(std::uintptr_t w,float){
    ++nativeHits;Vector v{};GetHitsHook(w+0x4c8,&v,w,0);
    if(v.begin){Check(v.end==v.begin+1&&v.begin->partId==7,"exact contact reaches native consumer");delete v.begin;++freed;}
}
void __fastcall NativeImpulse(void*,void*,const Vec3* d,int,std::uintptr_t,float,float,float){impulseDirection=*d;}
int __fastcall RigidAction(std::uintptr_t,const physical::Impulse* p,int threaded){Check(threaded==1,"native threadsafe impulse ABI");lastImpulse=*p;return 1;}
}
namespace preyvr::dll {
bool EnsureMinHook(){return false;}
DWORD InputDrainThreadId(){return drainThread;}
bool HudGameplayInputAllowed(){return !modal;}
unsigned long long HeadTrackingReferenceGeneration(){return 2;}
bool TryGetEquippedRig(std::uintptr_t,EquippedRig& rig){rig=currentRig;return true;}
bool QueryPhysicalSegment(const GameplayPoseFrame&,Vec3 origin,Vec3 delta,scene::Hit& h){
    ++rays;h={};h.distance=physical::Length(delta)*.5f;h.point=physical::Add(origin,physical::Scale(delta,.5f));
    h.normal={-1,0,0};h.collider=reinterpret_cast<void*>(1);h.partId=7;return !queryRefused;
}
void QueueHaptic(Hand,haptics::Event event,const TrackingFrame&){Check(event==haptics::Event::WeaponContact,"physical feedback event");++pulses;}
}
int main(){
    using namespace preyvr;using namespace preyvr::dll;using namespace preyvr::physical;
    module=reinterpret_cast<std::uintptr_t>(Append)-native::AppendHit;
    auto w=reinterpret_cast<std::uintptr_t>(weaponBytes);*reinterpret_cast<std::uintptr_t*>(w)=module+native::WrenchVtable;
    installed=true;originalAttack=NativeAttack;originalHit=NativeHit;originalGetHits=NativeGetHits;originalImpulse=NativeImpulse;
    Contact c{};c.found=true;c.hit.partId=7;c.hit.collider=reinterpret_cast<void*>(1);c.direction={1,0,0};c.speed=2;
    admit=false;Check(!Strike(w,c)&&nativeHits==0,"native fatigue refusal prevents damage call");
    admit=true;Check(Strike(w,c)&&nativeHits==1&&appends==1&&freed==1,"accepted native attack uses one engine-owned vector");
    HitHook(w,0);std::thread other([&]{HitHook(w,0);});other.join();
    Check(nativeHits==1&&suppressed==2,"late and duplicate animation hits suppressed across threads");
    const auto before=attacks;AttackHook(w,1,nullptr,1,1);Check(attacks==before,"button cannot chain during owned animation");
    AttackHook(w,1,nullptr,2,0);Check(attacks==before+1,"release still reaches native handler");
    SetPhysicalMelee(0);HitHook(w,0);Check(nativeHits==1,"option off cannot resurrect delayed damage");
    *reinterpret_cast<int*>(w+0x520)=0;AttackHook(w,1,nullptr,1,1);HitHook(w,0);
    Check(nativeHits==2&&appends==1,"next idle button attack restores native query");
    request={w,c.hit,c.direction};ImpulseHook(nullptr,nullptr,&c.direction,7,w,1,0,100);request={};
    Check(impulseDirection.x==-1,"negating native wrapper receives physical incoming direction");
    ImpulseHook(nullptr,nullptr,&c.direction,7,w,1,0,100);Check(impulseDirection.x==1,"ordinary native impulse unchanged");
    currentRig={w,10,11,12,1,42};GameplayPoseFrame f{};f.player=1;f.cameraCentreValid=true;f.headYawUsable=true;f.referenceGeneration=2;
    f.tracking.epoch=1;f.tracking.sequence=1;f.tracking.displayTime=1000000000;f.tracking.hands[1].gripValidity={true,true,true,true,0};
    f.tracking.headValidity={true,true,true,true,0};
    animik::Location location{};location.s=1;weaponrig::ContactGeometry shape{};shape.wrench=true;shape.minimum={-.05f,0,-.05f};shape.maximum={.05f,.6f,.05f};
    SetPhysicalMelee(1);*reinterpret_cast<int*>(w+0x520)=0;float x=0;
    auto tick=[&](float dx=0){x+=dx;++f.tracking.sequence;f.tracking.displayTime+=10000000;f.tracking.publishedNs=MonotonicNanoseconds();
        f.tracking.hands[1].gripPose.position.x=x;
        PublishPhysicalWeapon(f,currentRig,location,Pose{{},{x,0,0}},shape);UpdatePhysicalInteractions(f,true);
    };
    for(int i=0;i<25;++i)tick();const auto strikesBefore=strikes.load();
    for(int i=0;i<5;++i)tick(.025f);
    Check(strikes==strikesBefore+1&&pulses==1,"production path admits one strike and pulse");
    const auto raysBefore=rays;for(int i=0;i<10;++i)tick(.025f);Check(rays==raysBefore,"continued swing makes no repeat query");
    modal=true;tick();modal=false;tick(.04f);Check(rays==raysBefore,"menu exit requires fresh neutral rest");
    for(int i=0;i<40;++i)tick();queryRefused=true;for(int i=0;i<5;++i)tick(.025f);
    Check(strikes==strikesBefore+1,"query refusal cannot reuse last contact");queryRefused=false;
    SetPhysicalMelee(0);for(int i=0;i<20;++i)tick(.03f);Check(strikes==strikesBefore+1,"option disabled stops consumer");
    module=reinterpret_cast<std::uintptr_t>(RigidAction)-native::RigidAction;
    alignas(16) unsigned char rigid[0x400]{};auto& collider=*reinterpret_cast<std::uintptr_t*>(rigid);
    collider=module+native::RigidVtable;c.hit.collider=rigid;
    *reinterpret_cast<float*>(rigid+0x2e8)=1;
    for(unsigned i:{0u,4u,8u})*reinterpret_cast<float*>(rigid+0x328+i*4)=1;
    SetContactStrength(50);Check(NudgeRigid(c)&&lastImpulse.impulse.x==.5f&&lastImpulse.partId==7,"rigid nudge uses bounded momentum without melee call");
    collider=module+native::RigidVtable+8;Check(!NudgeRigid(c),"unknown classes receive no generic impulse");
    std::cout<<"Physical dispatch, duplicate suppression, ownership and rigid impulse ABI passed\n";
}
