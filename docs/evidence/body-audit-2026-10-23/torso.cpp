#include "AnimIkTakeover.cpp"
#include <iostream>
namespace {float roomYaw=0;}
namespace preyvr::lifecycle {void Log(std::string_view){}}
namespace preyvr::dll {
unsigned HandRigMode(){return 0;}
void PublishSupportGripGeometry(const SupportGripGeometry&){}
unsigned TwoHandedAimEnabled(){return 1;}
bool EnsureGameplayPoseObservation(){return false;}
bool TryGetGameplayPoseFrame(GameplayPoseFrame&,bool){return false;}
unsigned AimBodyYawEnabled(){return 1;}
bool TryGetEquippedRig(std::uintptr_t,EquippedRig&){return false;}
std::uint64_t WeaponEquipGeneration(){return 0;}
DWORD SetWeaponAttachmentObserving(unsigned){return 0;}
unsigned WeaponOffsetArmed(){return 0;}
unsigned WeaponRotationDriveArmed(){return 0;}
bool CarryHoldingInHand(){return false;}
bool GrenadeHeldByHand(std::uintptr_t){return false;}
void PublishPhysicalWeapon(const GameplayPoseFrame&,const EquippedRig&,const animik::Location&,Pose,const weaponrig::ContactGeometry&){}
bool PhysicalInteractionsEnabled(){return false;}
void PublishWristArm(const WristArmFrame&){}
bool ReadHeadTrackingRoom(float& yaw,Vec3& origin,unsigned long long& gen){yaw=roomYaw;origin={0,1.7f,0};gen=2;return true;}
bool EnsureMinHook(){return false;}
int MoveLaneAxisMilli(unsigned){return 0;}
bool HeadRelativeMovementEnabled(){return false;}
float CrouchEyeLift(){return 0;}
}
int main(){
 using namespace preyvr;using namespace preyvr::dll;
 gTorsoNow.store(.3f);gTorsoNowNs.store(preyvr::timing::MonotonicNanoseconds());
 float yaw=0;const bool valid=TorsoYawNow(yaw);
 std::cout<<"torso_cache_accepts_without_owner_epoch_reference="<<valid<<" yaw="<<yaw<<'\n';
 GameplayPoseFrame f{};f.tracking.epoch=1;f.referenceGeneration=2;f.tracking.head.position={0,1.7f,0};
 PoseSettings settings{};gTorso.Reset();UpdateTorso(f,settings);
 // A coordinate-system rebase, not a physical head turn. Both head and room
 // yaw change by +90deg, so their WORLD direction should remain unchanged.
 f.tracking.epoch=2;f.referenceGeneration=4;f.tracking.head.orientation={0,.70710678f,0,.70710678f};roomYaw=1.5707963268f;
 gTorsoNs=preyvr::timing::MonotonicNanoseconds()-13888889ull;
 const float retained=UpdateTorso(f,settings);gTorso.Reset();const float reset=UpdateTorso(f,settings);
 std::cout<<"reference_rebase_retained_torso_deg="<<retained*57.29578f<<" reset_control_deg="<<reset*57.29578f<<'\n';
 // Exercise PlanBody itself with two owning publications on either side of a
 // sub-metre world-position discontinuity. Pose, anchor and location agree.
 gBodyRig={};gBodyRig.valid=true;gBodyRig.skeleton=123;gRigSkeleton=123;gBodyRig.count=13;
 gBodyRig.hip=0;for(int k=0;k<4;++k)gBodyRig.spine[k]=k+1;gBodyRig.neck=5;gBodyRig.head=6;
 gBodyRig.thigh[0]=7;gBodyRig.knee[0]=8;gBodyRig.foot[0]=9;
 gBodyRig.thigh[1]=10;gBodyRig.knee[1]=11;gBodyRig.foot[1]=12;
 QuatT absolute[13]{};for(auto& a:absolute)a.q[3]=1;
 const Vec3 joints[13]={{0,0,.9f},{0,0,1},{0,0,1.15f},{0,0,1.3f},{0,0,1.4f},{0,0,1.5f},{0,0,1.65f},
   {.1f,0,.9f},{.1f,.08f,.5f},{.1f,0,.1f},{-.1f,0,.9f},{-.1f,.08f,.5f},{-.1f,0,.1f}};
 for(int k=0;k<13;++k){absolute[k].t[0]=joints[k].x;absolute[k].t[1]=joints[k].y;absolute[k].t[2]=joints[k].z;}
 f={};f.player=1;f.tracking.epoch=1;f.referenceGeneration=2;f.tracking.head.position={0,1.7f,0};
 f.cameraCentreValid=true;f.cameraCentre={0,0,1.7f};roomYaw=0;
 gBodyWeight=1;gBodyPoseOn=true;gFeetOn=true;gFeet.Reset();gHaveLastLocation=false;
 animik::Location location{};location.s=1;
 gBodyNs=preyvr::timing::MonotonicNanoseconds()-13888889ull;
 const auto first=PlanBody(f,location,reinterpret_cast<std::uint8_t*>(absolute),13,settings,0);
 f.player=2;f.tracking.epoch=2;location.t.x=.9f;f.cameraCentre.x=.9f;
 gBodyNs=preyvr::timing::MonotonicNanoseconds()-13888889ull;
 const auto moved=PlanBody(f,location,reinterpret_cast<std::uint8_t*>(absolute),13,settings,0);
 const auto gap=[&](const bodypose::Plan& p){return Dist3(bodypose::Apply(p.shin[0],joints[9]),bodypose::Apply(p.foot[0],joints[9]));};
 gFeet.Reset();const auto clean=PlanBody(f,location,reinterpret_cast<std::uint8_t*>(absolute),13,settings,0);
 std::cout<<"production_planbody_first_valid="<<first.valid<<" changed_owner_valid="<<moved.valid<<" retained_foot_gap_m="<<gap(moved)<<" reset_control_gap_m="<<gap(clean)<<'\n';
 return valid?0:1;
}
