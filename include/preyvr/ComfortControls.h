#pragma once
#include "preyvr/VrMath.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/MotionController.h"
#include <algorithm>
#include <cmath>
namespace preyvr::comfort {
class SnapLatch {
    bool ready_=false;
public:
    int Update(float x,float y,bool allowed) {
        if(!allowed || !std::isfinite(x) || !std::isfinite(y)) {ready_=false;return 0;}
        if(std::abs(x)<.3f && std::abs(y)<.3f) {ready_=true;return 0;}
        if(ready_ && std::abs(x)>.65f && std::abs(x)>std::abs(y)) {
            ready_=false;return x>0 ? 1 : -1;
        }
        return 0;
    }
};
inline Vec2 HeadRelativeStick(Vec2 stick,float yaw,float strafe,float backward) {
    if(!std::isfinite(yaw) || !std::isfinite(stick.x) || !std::isfinite(stick.y) ||
       !std::isfinite(strafe) || !std::isfinite(backward) || strafe<=0 || backward<=0) return {};
    const float c=std::cos(yaw),s=std::sin(yaw);
    Vec2 v{(c*stick.x-s*stick.y)/strafe,s*stick.x+c*stick.y};
    if(v.y<0) v.y/=backward;
    // Cancel native directional scaling without asking an axis for >100%.
    const float length=std::hypot(v.x,v.y),input=std::min(1.f,std::hypot(stick.x,stick.y));
    if(length>0) {v.x*=input/length;v.y*=input/length;}
    return v;
}
inline Vec3 TurnOriginAboutHead(Vec3 origin,Vec3 head,float referenceYawDelta) {
    const Quaternion rotation{0,std::sin(referenceYawDelta*.5f),0,std::cos(referenceYawDelta*.5f)};
    const Vec3 rotated=Rotate(rotation,{head.x-origin.x,head.y-origin.y,head.z-origin.z});
    return {head.x-rotated.x,head.y-rotated.y,head.z-rotated.z};
}
inline Vec3 RecenterOrigin(Vec3 head,Vec3 previous,bool preserveHeight) {
    if(preserveHeight) head.y=previous.y;
    return head;
}
// OpenXR's event gives new natural origin expressed in the previous space.
// Only gravity-aligned changes fit the yaw-only game reference contract.
inline bool RebaseReference(Vec3& origin,float& yaw,const Pose& previousFromNew) {
    if(!IsPoseUsable(previousFromNew,PoseValidity{true,true,true,true,0},0)) return false;
    const auto q=Normalize(previousFromNew.orientation);
    if(std::abs(q.x)>.001f || std::abs(q.z)>.001f) return false;
    const auto delta=stereo::RecenterYawFromHeadPose(previousFromNew);
    if(!delta) return false;
    origin=Rotate(stereo::Conjugate(q),{origin.x-previousFromNew.position.x,
        origin.y-previousFromNew.position.y,origin.z-previousFromNew.position.z});
    yaw=controller::SnapTurn(yaw,-1,*delta);
    return true;
}

}
