#pragma once
#include "preyvr/SceneQuery.h"
#include <algorithm>
#include <array>
#include <atomic>

namespace preyvr::physical {
inline Vec3 Add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 Scale(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float Length(Vec3 a){return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);}
inline Quaternion Inverse(Quaternion q){return {-q.x,-q.y,-q.z,q.w};}
inline Pose Inverse(Pose p){auto q=Inverse(p.orientation);return {q,Rotate(q,Scale(p.position,-1))};}
inline Vec3 Transform(Pose p,Vec3 v){return Add(p.position,Rotate(p.orientation,v));}
using Points=std::array<Vec3,9>;
inline Points Bounds(Vec3 lo,Vec3 hi){
    Points p{};p[0]=Scale(Add(lo,hi),.5f);
    for(unsigned i=0;i<8;++i)p[i+1]={i&1?hi.x:lo.x,i&2?hi.y:lo.y,i&4?hi.z:lo.z};
    return p;
}
struct Sample {
    Points points{}; // engine axes, XR reference space; never world history
    Pose grip{},worldFromReference{};
    std::uint64_t sequence=0,stamp=0,owner=0,epoch=0,reference=0;
};
struct Motion {Points from{},to{};float speed=0,dt=0;bool query=false,strike=false;};
// Controller motion admits a gesture; weapon geometry determines its contacts.
// Rebase both ends through the CURRENT world transform, excluding locomotion
// and snap turns. A fresh rest interval is required after every discontinuity.
class Swing {
public:
    void Reset(){*this={};}
    Motion Update(const Sample& s,float threshold){
        Motion m{};
        if(s.sequence==previous_.sequence&&s.owner==previous_.owner&&s.epoch==previous_.epoch&&s.reference==previous_.reference)return m;
        const bool same=s.owner&&s.owner==previous_.owner&&s.epoch==previous_.epoch&&s.reference==previous_.reference;
        const float dt=s.stamp>previous_.stamp?float(s.stamp-previous_.stamp)*1e-9f:0;
        if(!same||dt<.001f||dt>.1f){Reset();previous_=s;return m;}
        const float move=Length(Sub(s.grip.position,previous_.grip.position));
        const auto a=s.grip.orientation,b=previous_.grip.orientation;
        const float dot=std::clamp(std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w),0.f,1.f);
        const float angle=2*std::acos(dot);
        float radius=0,travel=0;
        for(unsigned i=0;i<s.points.size();++i){
            radius=std::max(radius,Length(Sub(s.points[i],s.grip.position)));
            travel=std::max(travel,Length(Sub(s.points[i],previous_.points[i])));
            m.from[i]=Transform(s.worldFromReference,previous_.points[i]);
            m.to[i]=Transform(s.worldFromReference,s.points[i]);
        }
        previous_=s;
        const float intent=(move+angle*std::min(radius,.8f))/dt;
        if(!std::isfinite(intent)||!std::isfinite(travel)||move>.35f||angle>1.1f||travel>.5f){Reset();previous_=s;return {};}
        m.dt=dt;m.speed=std::min(intent,travel/dt);
        cooldown_=std::max(0.f,cooldown_-dt);
        if(intent<.25f){rest_+=dt;if(rest_>=.15f){armed_=true;travel_=0;}}
        else rest_=0;
        if(armed_&&intent>=.25f)travel_+=std::min(travel,move+angle*radius);
        m.query=armed_&&cooldown_==0&&m.speed>=.25f&&travel>.001f;
        m.strike=m.query&&m.speed>=threshold&&travel_>=.09f;
        return m;
    }
    void Contact(){armed_=false;rest_=0;travel_=0;cooldown_=.35f;}
private:
    Sample previous_{};float rest_=0,travel_=0,cooldown_=0;bool armed_=false;
};
struct Contact {scene::Hit hit{};Vec3 direction{};float speed=0,fraction=2;bool found=false;};
// Query returns false on refusal, true on a valid hit OR miss (distance=-1).
// Failure of any segment refuses the batch. No partial-query hit can escape.
template<class Query> bool Sweep(const Motion& m,Query&& query,Contact& result){
    result={};Contact best{};
    for(unsigned i=0;i<m.from.size();++i){
        const auto delta=Sub(m.to[i],m.from[i]);const float length=Length(delta);
        if(length<.001f)continue;
        scene::Hit hit{};hit.distance=-1;
        if(!query(m.from[i],delta,hit))return false;
        if(hit.distance<0)continue;
        if(!hit.collider||!scene::Finite(hit.point)||!scene::Finite(hit.normal)||hit.distance>length+.001f)return false;
        const float fraction=hit.distance/length;
        if(fraction<best.fraction){best={hit,Scale(delta,1/length),std::min(m.speed,length/m.dt),fraction,true};}
    }
    result=best;return true;
}
// Steam pe_action_impulse, proved at producer 0x1231940 and rigid consumer
// 0xBCF990. Explicit vectors avoid the engine's uninitialised/unused sentinels.
struct Impulse {
    int type=2;Vec3 impulse{},angular{},point{};
    int partId=0,part=static_cast<int>(0x80000000u),applyTime=2,source=2;
};
static_assert(sizeof(Impulse)==0x38&&offsetof(Impulse,point)==0x1c&&offsetof(Impulse,source)==0x34);
inline Impulse Nudge(const Contact& c,unsigned percent){
    Impulse p{};p.point=c.hit.point;p.partId=c.hit.partId;
    p.impulse=Scale(c.direction,std::min(1.5f,c.speed*.5f)*std::min(percent,100u)*.01f);return p;
}
struct RigidResponse {float inverseMass=0;Vec3 centre{};std::array<float,9> inverseInertia{};};
// A fixed impulse alone can launch a tiny prop. Bound the induced velocity,
// including torque about the centre of mass, before asking native physics.
inline bool LimitNudge(Impulse& p,const RigidResponse& body){
    if(!std::isfinite(body.inverseMass)||body.inverseMass<=0||!scene::Finite(body.centre)||
       !scene::Finite(p.point)||!scene::Finite(p.impulse))return false;
    for(float f:body.inverseInertia)if(!std::isfinite(f))return false;
    const auto arm=Sub(p.point,body.centre),v=p.impulse;
    const Vec3 torque{arm.y*v.z-arm.z*v.y,arm.z*v.x-arm.x*v.z,arm.x*v.y-arm.y*v.x};
    const auto& a=body.inverseInertia;
    const Vec3 angular{a[0]*torque.x+a[1]*torque.y+a[2]*torque.z,
        a[3]*torque.x+a[4]*torque.y+a[5]*torque.z,a[6]*torque.x+a[7]*torque.y+a[8]*torque.z};
    const float linear=Length(v)*body.inverseMass,rotation=Length(angular);
    if(!std::isfinite(linear)||!std::isfinite(rotation))return false;
    const float scale=std::min({1.f,linear>1.5f?1.5f/linear:1.f,rotation>3.f?3.f/rotation:1.f});
    p.impulse=Scale(p.impulse,scale);return Length(p.impulse)>0;
}
// Own the whole native animation after an immediate VR impact. Keep the marker
// after its delayed event, so duplicate animation events cannot apply damage.
// A subsequent admitted idle attack restores the normal button path.
class HitOwnership {
public:
    bool Claim(std::uintptr_t weapon){
        if(!weapon)return false;
        if(Suppress(weapon))return true;
        for(auto& entry:weapons_){std::uintptr_t empty=0;if(entry.compare_exchange_strong(empty,weapon))return true;}
        return false; // Never evict a weapon which can still owe a delayed hit.
    }
    void Release(std::uintptr_t weapon){for(auto& entry:weapons_){auto expected=weapon;entry.compare_exchange_strong(expected,0);}}
    bool Suppress(std::uintptr_t weapon)const{
        if(weapon)for(const auto& entry:weapons_)if(entry.load()==weapon)return true;
        return false;
    }
    bool BlockAttack(std::uintptr_t weapon,int state,int activation){
        if(!Suppress(weapon))return false;
        if(state==0){Release(weapon);return false;}
        return activation!=2;
    }
private:std::array<std::atomic<std::uintptr_t>,16> weapons_{};
};
}
