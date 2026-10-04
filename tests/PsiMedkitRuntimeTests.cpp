// Production hooks with mocks only at the native ABI and pose boundaries.
#include <windows.h>
#include <cstring>
HMODULE TestModule(const wchar_t*);
#define GetModuleHandleW TestModule
#include "../src/dll/PsiMedkit.cpp"
#undef GetModuleHandleW
#include <cstdlib>
#include <iostream>
#include <thread>
namespace {
using namespace preyvr;using namespace preyvr::dll;
alignas(16) unsigned char playerBytes[0x2000]{},psiBytes[0x600]{},focusBytes[0x400]{};
std::uintptr_t moduleBase=0,player=0,power=0,focus=0;
GameplayPoseFrame published{};bool modal=false,poseAvailable=true,selectAllowed=true,clearOnActivate=false;
unsigned updates=0,activations=0,selections=0,starts=0,stops=0,consumed=0,pulses=0,foreignActivations=0;
int consumeResult=2;unsigned long long reference=2,menuEpoch=1;
const DWORD drainThread=GetCurrentThreadId();psi::Ray observed{},castRay{};
void Check(bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
bool Near(float a,float b){return std::fabs(a-b)<.0001f;}
template<class T> T& Field(std::uintptr_t p,unsigned offset){return *reinterpret_cast<T*>(p+offset);}
std::uintptr_t __fastcall GetPlayer(){return player;}
std::uintptr_t __fastcall GetPower(std::uintptr_t p){Check(p==player,"whole player receiver");return power;}
std::uintptr_t __fastcall MockGetFocus(std::uintptr_t p){Check(p==player+0x678,"focus accessor secondary receiver");return focus;}
psi::Ray* __fastcall GetRay(std::uintptr_t,psi::Ray* out){*out={{7,8,9},{1,0,0}};return out;}
void __fastcall Update(std::uintptr_t p,float){Check(p==power,"psi update receiver");++updates;RayHook(player+0x40,&observed);}
bool __fastcall Activate(std::uintptr_t p){if(p!=power){++foreignActivations;return true;}++activations;RayHook(player+0x40,&castRay);
    if(clearOnActivate)Field<int>(power,0x230)=-1;return true;}
bool __fastcall Select(std::uintptr_t p,int id){Check(p==power,"psi select receiver");++selections;if(selectAllowed)Field<int>(p,0x230)=id;return selectAllowed;}
bool __fastcall Candidate(std::uintptr_t,std::uintptr_t,bool preferred){return preferred;}
bool __fastcall NativeStart(std::uintptr_t p,bool ui){Check(p==focus&&!ui,"native hidden focus mode");++starts;
    if(Field<unsigned char>(p,0x390))return false;Field<unsigned char>(p,0x390)=1;return true;}
void __fastcall Stop(std::uintptr_t p,bool){Check(p==focus,"live focus receiver on stop");++stops;Field<unsigned char>(p,0x390)=0;Field<int>(power,0x230)=-1;}
std::uint64_t __fastcall MedkitId(){return 0x123456789abcdef0ull;}
int __fastcall Consume(std::uint64_t id){Check(id==MedkitId(),"full 64-bit medkit archetype");++consumed;return consumeResult;}
void Jump(std::uintptr_t rva,const void* fn){
    unsigned char code[12]{0x48,0xb8};std::memcpy(code+2,&fn,8);code[10]=0xff;code[11]=0xe0;
    std::memcpy(reinterpret_cast<void*>(moduleBase+rva),code,sizeof(code));
}
void Tick(bool fire=false,bool grip=false,bool valid=true,float dt=.01f){
    published.tracking.displayTime+=static_cast<long long>(dt*1e9f);++published.tracking.sequence;
    published.tracking.publishedNs=MonotonicNanoseconds();published.tracking.hands[0].triggerPressed=fire;
    published.tracking.hands[0].gripPressed=grip;UpdatePsiMedkit(published,valid);
}
}
HMODULE TestModule(const wchar_t*){return reinterpret_cast<HMODULE>(moduleBase);}
namespace preyvr::dll {
void PublishSlotFeedback(equipment::SlotNotice,const GameplayPoseFrame&){}
bool EnsureMinHook(){return false;}
bool EnsureGameplayPoseObservation(){return true;}
DWORD InputDrainThreadId(){return drainThread;}
unsigned long long HeadTrackingReferenceGeneration(){return reference;}
bool HudGameplayInputAllowed(){return !modal;}
unsigned long long HudMenuEpoch(){return menuEpoch;}
bool TryGetGameplayPoseFrame(GameplayPoseFrame& out,bool){out=published;return poseAvailable;}
void QueueHaptic(Hand hand,haptics::Event,const TrackingFrame&){Check(hand==Hand::left,"left feedback");++pulses;}
}
int main(){
    using namespace preyvr;using namespace preyvr::dll;
    moduleBase=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x2000000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(moduleBase!=0,"test ABI arena");player=reinterpret_cast<std::uintptr_t>(playerBytes);
    Check(Gate()==ERROR_BAD_EXE_FORMAT,"unsupported native bodies refused before installing hooks");
    power=reinterpret_cast<std::uintptr_t>(psiBytes)+8;focus=reinterpret_cast<std::uintptr_t>(focusBytes);
    Jump(native::GetPlayer,reinterpret_cast<const void*>(&GetPlayer));Jump(native::GetPower,reinterpret_cast<const void*>(&GetPower));
    Jump(native::GetFocus,reinterpret_cast<const void*>(&MockGetFocus));Jump(native::FocusStart,reinterpret_cast<const void*>(&NativeStart));
    Jump(native::FocusStop,reinterpret_cast<const void*>(&Stop));Jump(native::Select,reinterpret_cast<const void*>(&SelectHook));
    Jump(native::Activate,reinterpret_cast<const void*>(&ActivateHook));Jump(native::MedkitArchetype,reinterpret_cast<const void*>(&MedkitId));
    Jump(native::Consume,reinterpret_cast<const void*>(&Consume));FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(moduleBase),0x2000000);
    Field<std::uintptr_t>(player,0x678)=power-8;Field<std::uintptr_t>(player,0x1810)=1;
    Field<std::uintptr_t>(power-8,0x118)=focus;Field<int>(power,0x230)=2;Field<int>(power,0x234)=2;
    Field<std::uintptr_t>(power,0x40+16)=0xabcdef;
    Field<std::uintptr_t>(player+scope::InputOffset,0)=moduleBase+scope::InputVtableRva;
    Field<std::uintptr_t>(player+scope::InputOffset,0x70)=player;
    const psi::Ray weapon{{70,80,90},{0,1,0}};Field<psi::Ray>(player,0x17d4)=weapon;
    originalUpdate=Update;originalActivate=Activate;originalSelect=Select;originalRay=GetRay;originalCandidate=Candidate;installed=true;
    published.player=player;published.cameraCentreValid=published.headYawUsable=true;
    published.cameraCentre={12,23,3};published.referenceGeneration=2;published.tracking.epoch=1;
    published.tracking.head={{},{0,1.65f,0}};published.tracking.headValidity={true,true,true,true,0};
    auto& left=published.tracking.hands[0];left.aimPose={{},{-.3f,1.2f,-.4f}};left.gripPose={{},{-.25f,1.f,.02f}};
    left.aimValidity=left.gripValidity={true,true,true,true,0};Tick();Tick();
    psi::Ray outside{};RayHook(player+0x40,&outside);UpdateHook(power,.01f);
    Check(outside.origin.x==7&&observed.origin.x==7&&updates==1,"original mode unchanged");
    Check(SetPsiTargetMode(1)==0,"enable head targeting");UpdateHook(power,.01f);
    Check(Near(observed.origin.x,12)&&Near(observed.direction.y,1)&&updates==2,"one native update with head ray");
    RayHook(player+0x40,&outside);Check(outside.origin.x==7,"weapon getter outside scope unchanged");
    psi::Context scoped{preview,true,0};context=&scoped;RayHook(player+0x48,&outside);context=nullptr;
    Check(outside.origin.x==7,"wrong secondary receiver stays native");
    scoped.reads=1;context=&scoped;Check(!CandidateHook(1,2,true),"HUD candidate must pass VR angle test");context=nullptr;
    Check(CandidateHook(1,2,true),"unscoped native candidate preference preserved");
    Check(ActivateHook(power+64)&&foreignActivations==1&&activations==0,"other components keep their native activation");
    published.cameraCentre.x=20;
    Check(ActivateHook(power)&&Near(castRay.origin.x,12)&&activations==1,"cast uses preview ray rather than newer pose");
    Check(!ActivateHook(power)&&activations==1,"no duplicate preview cast");
    UpdateHook(power,.01f);++reference;Check(!ActivateHook(power)&&activations==1,"reference change refuses cast");reference=2;
    Check(SetPsiTargetMode(2)==0,"enable left-controller targeting");UpdateHook(power,.01f);
    Check(Near(observed.origin.x,19.7f)&&Near(observed.origin.y,23.4f),"left ray anchored to coherent head offset");
    std::thread other([&]{UpdateHook(power,.01f);Check(observed.origin.x==7,"foreign thread keeps native ray");});other.join();
    Check(Field<psi::Ray>(player,0x17d4).origin.x==weapon.origin.x&&Field<psi::Ray>(player,0x17d4).direction.y==1,"cached weapon fields unchanged");
    Field<int>(power,0x230)=-1;Tick();Tick();const auto startsBefore=starts,stopsBefore=stops,castsBefore=activations;
    Tick(true);Check(starts==startsBefore+1&&selections==1,"fresh press starts and selects once");
    UpdateHook(power,.01f);Tick(true);Check(starts==startsBefore+1,"hold does not repeat native selection");
    clearOnActivate=true;Tick(false);clearOnActivate=false;
    Check(activations==castsBefore+1&&stops==stopsBefore+1&&!Field<unsigned char>(focus,0x390),"release clears focus even when the native power clears its selection");
    Tick(true);UpdateHook(power,.01f);const auto canceled=activations;modal=true;Tick(true);modal=false;Tick(true);Tick(false);
    Check(activations==canceled&&!Field<unsigned char>(focus,0x390),"menu cancellation requires fresh press");
    Tick(true);UpdateHook(power,.01f);Tick(true,false,true,.5f);Tick(false);
    Check(activations==canceled&&!Field<unsigned char>(focus,0x390),"long frame gap cancels pending cast");
    Tick(true);UpdateHook(power,.01f);menuEpoch+=2;Tick(false);Tick();
    Check(activations==canceled&&!Field<unsigned char>(focus,0x390),"menu opened and closed between player updates cancels cast");
    selectAllowed=false;Tick(true);Check(!Field<unsigned char>(focus,0x390),"native selection refusal releases focus");
    Tick(false);selectAllowed=true;SetPsiTargetMode(0);SetMedkitSlot(1);Tick();Tick();
    const auto consumedBefore=consumed,startsForSlot=starts;
    Tick(false,true);Check(MedkitOwnsGrip()&&consumed==consumedBefore,"hip grab does not consume");
    left.gripPose.position={-.3f,1.3f,-.4f};Tick(true,true);
    Check(consumed==consumedBefore+1&&medkitUsed==1&&starts==startsForSlot,"one medkit use without psi input conflict");
    Tick(false,true);Tick(true,true);Check(consumed==consumedBefore+1,"one native transaction per grip");
    for(int result:{0,1}){Tick();left.gripPose.position={-.25f,1.f,.02f};Tick(false,true);consumeResult=result;Tick(true,true);}
    Check(medkitAbsent==1&&medkitDenied==1,"absent and native-denied results remain distinct");
    Tick(false,true,false);Tick(true,true);Check(!MedkitOwnsGrip(),"tracking loss requires fresh grip");
    Tick();Tick(false,true);const auto blocked=consumed;modal=true;Tick(true,true);modal=false;Tick(true,true);
    Check(consumed==blocked,"modal ownership prevents medkit use");
    Tick();SetMedkitSlot(0);Tick();Tick(false,true);Tick(true,true);Check(consumed==blocked,"disabled slot has no transaction");
    Check(SetPsiTargetMode(3)==ERROR_INVALID_PARAMETER&&SetMedkitSlot(2)==ERROR_INVALID_PARAMETER,"invalid settings refused");
    VirtualFree(reinterpret_cast<void*>(moduleBase),0,MEM_RELEASE);
    std::cout<<"Production psi scopes, preview provenance, native focus cancellation and medkit ABI passed\n";
}
