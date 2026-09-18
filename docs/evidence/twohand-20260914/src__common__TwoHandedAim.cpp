#include "preyvr/TwoHandedAim.h"
#include <algorithm>
#include <cmath>

namespace preyvr::twohand {
namespace {
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 Add(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vec3 Mul(Vec3 a,float s) { return {a.x*s,a.y*s,a.z*s}; }
float Dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
bool Finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
bool Valid(Quaternion q) { const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w; return std::isfinite(n)&&n>.81f&&n<1.21f; }
Quaternion Inv(Quaternion q) { return {-q.x,-q.y,-q.z,q.w}; }
Quaternion Mix(Quaternion a,Quaternion b,float t) {
    float d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(d<0) { b={-b.x,-b.y,-b.z,-b.w};d=-d; }
    float x=1-t,y=t;
    if(d<.9995f) { const float angle=std::acos(std::clamp(d,0.f,1.f)); const float s=std::sin(angle);
        x=std::sin((1-t)*angle)/s;y=std::sin(t*angle)/s; }
    return Normalize({a.x*x+b.x*y,a.y*x+b.y*y,a.z*x+b.z*y,a.w*x+b.w*y});
}
Quaternion Swing(Vec3 a,Vec3 b) {
    a=Mul(a,1/std::sqrt(Dot(a,a))); b=Mul(b,1/std::sqrt(Dot(b,b)));
    const float d=std::clamp(Dot(a,b),-1.f,1.f);
    if(d<-.9999f) {
        // Stable 180-degree branch; no world-up singularity for vertical aim.
        const Vec3 axis=std::fabs(a.x)<.8f?Vec3{1,0,0}:Vec3{0,0,1};
        const Vec3 perpendicular=Sub(axis,Mul(a,Dot(axis,a)));
        return Normalize({perpendicular.x,perpendicular.y,perpendicular.z,0});
    }
    return Normalize({a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x,1+d});
}
bool ValidInput(const Input& i) {
    return i.usable&&i.owner&&Finite(i.primary)&&Finite(i.support.position)&&
        Finite(i.visualPrimaryOffset)&&Valid(i.aim.orientation)&&Valid(i.support.orientation)&&
        std::isfinite(i.squeeze)&&i.squeeze>=0&&i.squeeze<=1&&std::isfinite(i.dt)&&i.dt>0&&i.dt<=.2f;
}
}
bool ClosestGrip(const Input& i,Vec3& point) {
    if(!ValidInput(i)||!Finite(i.region.start)||!Finite(i.region.end)||
       !std::isfinite(i.region.radius)||i.region.radius<=0||i.region.radius>.25f) return false;
    const Vec3 local=Rotate(Inv(i.aim.orientation),Sub(i.support.position,Add(i.primary,i.visualPrimaryOffset)));
    const Vec3 segment=Sub(i.region.end,i.region.start);
    const float n=Dot(segment,segment);
    const float t=n>1e-8f?std::clamp(Dot(Sub(local,i.region.start),segment)/n,0.f,1.f):0;
    point=Add(i.region.start,Mul(segment,t));
    // Long-weapon support only. A native offhand beside/behind the grip is not a foregrip.
    const float length=Dot(point,point);
    return point.y>=.12f&&length>=.0144f&&length<=.64f&&
        Dot(Sub(local,point),Sub(local,point))<=i.region.radius*i.region.radius;
}
Output Solver::Update(const Input& i) {
    Output out{};out.orientation=i.aim.orientation;
    const bool changed=i.owner!=owner_||i.reference!=reference_||i.epoch!=epoch_;
    if(changed||!ValidInput(i)) {
        *this={}; owner_=i.owner;reference_=i.reference;epoch_=i.epoch;
        // A weapon switch, modal transition or tracking loss requires a new squeeze.
        armed_=ValidInput(i)&&i.squeeze<.45f;
        return out;
    }
    if(i.squeeze<.45f) { held_=false;armed_=true; }
    else if(!held_&&armed_&&i.squeeze>=.65f) {
        armed_=false;
        if(ClosestGrip(i,socket_)) {
            held_=true;
            supportInAim_=Multiply(Inv(i.aim.orientation),i.support.orientation);
        }
    }
    const Vec3 line=Sub(i.support.position,Add(i.primary,i.visualPrimaryOffset));
    if(held_&&Dot(line,line)<.0144f) { held_=false;armed_=false;blend_=0;swing_={}; }
    if(held_) {
        // Raw controllers steer. The displayed support hand never feeds back here.
        const Vec3 localLine=Rotate(Inv(i.aim.orientation),line);
        swing_=Swing(socket_,localLine);
    }
    const float alpha=1-std::exp(-i.dt*25.f);
    blend_+=(held_?1-blend_:-blend_)*alpha;
    if(!held_&&blend_<.001f) { blend_=0;swing_={}; }
    const Quaternion local=Mix({},swing_,blend_);
    out.orientation=Normalize(Multiply(i.aim.orientation,local));
    out.correction=Normalize(Multiply(out.orientation,Inv(i.aim.orientation)));
    out.supportOrientation=Mix(i.support.orientation,Normalize(Multiply(out.orientation,supportInAim_)),blend_);
    out.socket=socket_;out.blend=blend_;out.held=held_;
    return out;
}
} // namespace preyvr::twohand
