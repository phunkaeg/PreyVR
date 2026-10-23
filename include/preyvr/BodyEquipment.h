#pragma once
#include "preyvr/VrMath.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <optional>

namespace preyvr::equipment {
// Metres in a yaw-only torso estimate. Hip and cross-body chest are reachable
// without putting a tracked controller behind the wearer.
inline constexpr std::array<Vec3,2> Zones{{{.25f,-.65f,.02f},{-.20f,-.30f,-.13f}}};
class HolsterGesture {
public:
 explicit HolsterGesture(std::array<Vec3,2> zones=Zones):zones_(zones){}
 int Update(Pose head,Vec3 hand,bool squeeze,bool valid,float dt) {
  if(!valid||!std::isfinite(dt)||dt<=0||dt>.2f){Reset();return -1;}
  auto forward=Rotate(head.orientation,{0,0,-1});
  const float length=std::hypot(forward.x,forward.z);
  if(length<.2f){Reset();return -1;}
  const float yaw=std::atan2(-forward.x,-forward.z);
  if(!bodyValid_){bodyYaw_=yaw;bodyValid_=true;}
  const auto local=[&](){return Rotate(Quaternion{0,-std::sin(bodyYaw_/2),0,std::cos(bodyYaw_/2)},
      Vec3{hand.x-head.position.x,hand.y-head.position.y,hand.z-head.position.z});};
  auto distance=[](Vec3 a,Vec3 b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));};
  auto point=local();int zone=-1;
  for(int n=0;n<2;++n)if(distance(point,zones_[n])<Radius)zone=n;
  // A head turn alone cannot drag an already-reached holster under the hand.
  // Outside both zones follow only beyond a 45-degree head/body deadband.
  if(!squeeze&&zone<0){
   const float delta=std::remainder(yaw-bodyYaw_,6.283185307f);
   if(std::fabs(delta)>.78539816f)bodyYaw_+=std::copysign(std::min(std::fabs(delta)-.78539816f,dt*1.57f),delta);
  }
  if(!squeeze){armed_=true;owned_=false;return -1;}
  if(!armed_)return -1;
  armed_=false;
  if(zone>=0){owned_=true;return zone;}
  return -1; // no activation by drifting into a zone with grip already held
 }
 bool OwnsGrip()const{return owned_;}
 void Reset(){armed_=false;bodyValid_=false;owned_=false;}
 // Diagnostics (dbg.draw): the torso estimate this gesture measures against.
 bool BodyValid()const{return bodyValid_;}
 bool Armed()const{return armed_;}
 Vec3 ZoneCentre(Vec3 head,int n)const{
  const auto z=Rotate(Quaternion{0,std::sin(bodyYaw_/2),0,std::cos(bodyYaw_/2)},zones_[n]);
  return {head.x+z.x,head.y+z.y,head.z+z.z};
 }
 static constexpr float Radius=.14f;
private:
 std::array<Vec3,2> zones_;
 bool armed_=false,owned_=false,bodyValid_=false;float bodyYaw_=0;
};
struct Vitals {
 int health=-1,maxHealth=-1,psi=-1,maxPsi=-1,suit=-1;
 bool operator==(const Vitals&)const=default;
};
inline int DisplayValue(float v,float scale=1.f){
 if(!std::isfinite(v)||v<0||v>100000)return -1;
 return static_cast<int>(std::ceil(v*scale));
}
// Grip-local card: over the inner left wrist, extending back towards the arm.
// +Z is the visible face of an OpenXR quad. Palm-up inspection faces it at eyes.
inline Pose WristPose(Pose grip){return Compose(grip,Pose{{-.70710678f,0,0,.70710678f},{0,.045f,.11f}});}
// The three numbers the visibility test compares: eye distance, how squarely
// the card faces the eye, and how close to the gaze centre it sits (cosines).
struct WristView {float distance=0,facing=0,viewing=0;};
inline WristView MeasureWrist(Pose head,Pose wrist){
 const Vec3 toEye{head.position.x-wrist.position.x,head.position.y-wrist.position.y,head.position.z-wrist.position.z};
 const float d=std::sqrt(toEye.x*toEye.x+toEye.y*toEye.y+toEye.z*toEye.z);
 if(!std::isfinite(d)||d<=0)return {d,0,0};
 const auto face=Rotate(wrist.orientation,{0,0,1});
 const auto look=Rotate(head.orientation,{0,0,-1});
 return {d,(face.x*toEye.x+face.y*toEye.y+face.z*toEye.z)/d,-(look.x*toEye.x+look.y*toEye.y+look.z*toEye.z)/d};
}
inline constexpr float WristFacingNeeded(bool wasVisible){return wasVisible?.35f:.55f;}
inline constexpr float WristViewingNeeded(bool wasVisible){return wasVisible?.5f:.65f;}
inline bool WristVisible(Pose head,Pose wrist,bool wasVisible){
 const auto m=MeasureWrist(head,wrist);
 if(!std::isfinite(m.distance)||m.distance<.23f||m.distance>1.f)return false;
 return m.facing>WristFacingNeeded(wasVisible)&&m.viewing>WristViewingNeeded(wasVisible);
}
}
