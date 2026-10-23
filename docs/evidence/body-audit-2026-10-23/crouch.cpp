// Review harness: includes the unchanged production adapter. Native calls are
// replaced only at boundaries; no Prey process or OpenXR session is created.
#include "BodyFollow.cpp"
#include <iostream>
#include <stdexcept>
namespace {
using namespace preyvr;using namespace preyvr::dll;
unsigned taps=0;bool room=false;
TrackingFrame sample{};
alignas(16) unsigned char actorA[0x1900]{},actorB[0x1900]{};
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void Reset(){gCrouchOwned=gCrouchSeen=gHaveStandingEye=gHaveEye=false;gCrouchAskedNs=0;
 gEyeStill=0;gStandingEye=0;gEyeLift=0;gStance=0;gCrouchOn=true;gInstalled=false;taps=0;room=false;}
void Eye(float z){NoteGameView({0,0,z},Quaternion{});}
void Stance(unsigned char* actor,int value){std::memcpy(actor+kPlayerStance,&value,sizeof(value));}
void Crouch(unsigned char* actor,float drop){
 const Vec3 origin{0,1.7f,0},neck{0,1.7f-.075f-drop,0};UpdateCrouch(actor,neck,origin,kStateGround);
}
}
namespace preyvr::lifecycle {void Log(std::string_view){}}
namespace preyvr::dll {
bool EnsureMinHook(){return false;}
bool HudGameplayInputAllowed(){return true;}
int MoveLaneCinematicGate(){return 0;}
int MoveLaneAxisMilli(unsigned){return 0;}
void RequestCrouchTap(){++taps;}
bool TryGetTrackingFrame(TrackingFrame& f){f=sample;return true;}
bool ReadHeadTrackingRoom(float& yaw,Vec3& origin,unsigned long long& gen){yaw=0;origin={0,1.7f,0};gen=2;return room;}
bool AdjustHeadTrackingRoom(float,const Vec3&,unsigned long long){return true;}
bool QueryBodySegment(std::uintptr_t,Vec3,Vec3,scene::Hit&){return false;}
bool NewestWristArm(WristArmFrame&){return false;}
float ViewHookLastNearPlane(){return .1f;}
DWORD SetBodyPose(unsigned){return 0;}
DWORD SetBodyFeet(unsigned){return 0;}
std::string BodyPoseReport(){return {};}
}
int main(){try{
 using namespace preyvr;using namespace preyvr::dll;
 Reset();for(int n=0;n<20;++n)Eye(1.7f);
 Check(gHaveStandingEye,"standing eye positive control");
 Stance(actorA,kStanceStand);Crouch(actorA,.45f);Check(taps==1&&gCrouchOwned,"physical crouch positive control");
 Stance(actorA,kStanceSneak);Crouch(actorA,.45f);Eye(1.0f);
 Check(CrouchEyeLift()>.69f,"owned crouch compensation positive control");
 // A newly loaded/replaced player has its own native crouch. There is no owner
 // or epoch token in the adapter, so the previous player's owner bit is reused.
 const auto before=taps;Stance(actorB,kStanceSneak);gCrouchAskedNs=MonotonicNanoseconds()-600000000ull;
 Crouch(actorB,0);
 std::cout<<"replacement_player_inherited_stand_taps="<<taps-before<<" owned="<<gCrouchOwned<<'\n';
 // A different valid standing camera height cannot relearn while old ownership
 // remains. Feed many stable standing frames; native calls stay substituted.
 Stance(actorB,kStanceStand);SetPhysicalCrouch(0);
 for(int n=0;n<120;++n){Eye(1.5f);Crouch(actorB,0);}
 std::cout<<"new_standing_baseline_owned="<<gCrouchOwned<<" eye_lift_m="<<CrouchEyeLift()<<" old_standing_m="<<gStandingEye<<'\n';
 Reset();Stance(actorA,kStanceStand);Crouch(actorA,.45f);Stance(actorA,kStanceSneak);
 for(int n=0;n<30;++n){Crouch(actorA,.45f);Eye(1.0f);}
 std::cout<<"crouch_without_learned_baseline_owned="<<gCrouchOwned<<" baseline_valid="<<gHaveStandingEye<<" eye_lift_m="<<CrouchEyeLift()<<'\n';
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
