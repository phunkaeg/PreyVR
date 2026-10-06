#pragma once
#include "preyvr/VrMath.h"
#include <cmath>
namespace preyvr {
// Head-local metres, OpenXR axes. Deliberate fresh squeeze followed by vertical
// travel; no action on head proximity alone. Native game owns scope/unlock state.
class PsychoscopeGesture {
public:
 // Head-local zone in front of the face where a squeeze starts the gesture.
 static bool InZone(Vec3 hand){return std::fabs(hand.x)<.27f&&hand.z<.12f&&hand.z>-.34f&&hand.y>-.24f&&hand.y<.34f;}
 bool Update(Vec3 hand,bool grip,bool usable,float dt,std::uint64_t generation) {
  if(!usable||generation!=generation_||!std::isfinite(dt)||dt<=0||dt>.2f||
     !std::isfinite(hand.x)||!std::isfinite(hand.y)||!std::isfinite(hand.z)){
   *this={};generation_=generation;return false;
  }
  if(!grip){armed_=true;drag_=false;time_=0;return false;}
  const bool inZone=InZone(hand);
  if(armed_){armed_=false;if(inZone){drag_=true;start_=hand;time_=0;}return false;}
  if(!drag_)return false;
  time_+=dt;
  if(!inZone||time_>1.5f||std::fabs(hand.x-start_.x)>.16f||std::fabs(hand.z-start_.z)>.18f){drag_=false;return false;}
  // A down stroke starts at the forehead; an up stroke starts by the eyes.
  const bool down=start_.y>=.06f&&hand.y<=start_.y-.14f;
  const bool up=start_.y<=.10f&&hand.y>=start_.y+.14f;
  if(time_>=.12f&&(down||up)){drag_=false;return true;}
  return false;
 }
private:
 Vec3 start_{};bool armed_=false,drag_=false;float time_=0;std::uint64_t generation_=0;
};
}
