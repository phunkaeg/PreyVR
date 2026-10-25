#include "preyvr/SurfaceGrab.h"
#include <algorithm>
#include <cmath>

namespace preyvr::surfacegrab {
namespace {
Vec3 Add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 Scale(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float Length(Vec3 a){return std::sqrt(Dot(a,a));}
bool Same(const Input& a,const Input& b){return a.player==b.player&&a.epoch==b.epoch&&a.reference==b.reference&&a.weapon==b.weapon&&a.generation==b.generation;}
bool GoodRotation(Quaternion q){const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;return std::isfinite(n)&&n>.5f&&n<2.f;}
float Turn(Quaternion a,Quaternion b){a=Normalize(a);b=Normalize(b);return 2*std::acos(std::clamp(std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w),0.f,1.f));}
bool Good(const Settings& s){return std::isfinite(s.reach)&&s.reach>=.03f&&s.reach<=.25f&&std::isfinite(s.pushScale)&&s.pushScale>=0&&s.pushScale<=2&&
    std::isfinite(s.maxPush)&&s.maxPush>=.1f&&s.maxPush<=3&&std::isfinite(s.maxPull)&&s.maxPull>0&&s.maxPull<=3&&
    std::isfinite(s.correction)&&s.correction>0&&s.correction<=20&&std::isfinite(s.maxStretch)&&s.maxStretch>s.reach&&s.maxStretch<=1&&
    std::isfinite(s.maxHandJump)&&s.maxHandJump>0&&std::isfinite(s.maxHandSpeed)&&s.maxHandSpeed>0&&
    std::isfinite(s.minLaunch)&&s.minLaunch>=0&&s.maxAge&&s.maxGap&&s.minHold;}
}
bool Finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
Vec3 Limit(Vec3 v,float maximum){const float n=Length(v);return std::isfinite(n)&&n>maximum?Scale(v,maximum/n):v;}
bool ValidContact(const Contact& c){const float n=Length(c.normal);return c.valid&&c.collider&&Finite(c.point)&&Finite(c.normal)&&n>.98f&&n<1.02f;}
bool SameContact(const Contact& a,const Contact& b){return ValidContact(a)&&ValidContact(b)&&a.collider==b.collider&&a.part==b.part&&
    Length(Sub(a.point,b.point))<=.03f&&Dot(a.normal,b.normal)>.95f;}
void Grab::Cancel(){have_=held_=armed_=false;previous_={};anchor_={};target_=velocity_={};start_=0;}
Output Grab::Update(const Input& i,const Settings& s){
    const bool wasHeld=held_;
    const auto cancel=[&]{Cancel();Output o{};o.cancelled=wasHeld;return o;};
    if(!Good(s)||!i.valid||!i.allowed||i.busy||!i.player||(i.reference&1)||!i.sequence||!i.stamp||i.now<i.stamp||i.now-i.stamp>s.maxAge||
       !Finite(i.localHand)||!Finite(i.worldHand)||!GoodRotation(i.worldFromReference))return cancel();
    if(have_&&!Same(i,previous_))return cancel();
    bool fresh=true;
    if(have_){
        if(i.now<previous_.now||i.sequence<previous_.sequence||i.stamp<previous_.stamp)return cancel();
        if(i.sequence==previous_.sequence){
            if(i.stamp!=previous_.stamp||i.grip!=previous_.grip||Length(Sub(i.localHand,previous_.localHand))>.000001f)return cancel();
            fresh=false;
        }else if(i.stamp<=previous_.stamp||i.stamp-previous_.stamp>s.maxGap)return cancel();
    }
    if(held_){
        if(!SameContact(anchor_,i.contact)||Length(Sub(target_,i.worldHand))>s.maxStretch||Turn(capturedRotation_,i.worldFromReference)>.10f)return cancel();
        if(fresh){
            const float dt=static_cast<float>(i.stamp-previous_.stamp)*1e-9f;
            const Vec3 delta=Sub(i.localHand,previous_.localHand);
            if(!(dt>0)||Length(delta)>s.maxHandJump||Length(delta)/dt>s.maxHandSpeed)return cancel();
            const float a=1-std::exp(-dt/.025f);
            velocity_=Add(Scale(velocity_,1-a),Scale(delta,a/dt));
        }
        if(!i.grip){
            held_=false;armed_=true;Output out{};out.released=true;
            if(i.stamp-start_>=s.minHold){
                Vec3 v=Scale(Rotate(i.worldFromReference,velocity_),-s.pushScale);
                const float into=Dot(v,anchor_.normal);if(into<0)v=Sub(v,Scale(anchor_.normal,into));
                v=Limit(v,s.maxPush);if(Length(v)>=s.minLaunch){out.velocity=v;out.launch=out.command=true;}
            }
            previous_=i;have_=true;return out;
        }
        Output out{};out.held=out.command=true;
        out.velocity=Limit(Add(Scale(Rotate(i.worldFromReference,velocity_),-1),Scale(Sub(target_,i.worldHand),s.correction)),s.maxPull);
        if(fresh)previous_=i;else {previous_.now=i.now;previous_.worldHand=i.worldHand;}
        return out;
    }
    Output out{};
    if(!i.grip)armed_=true;
    else if(armed_){
        armed_=false;
        if(ValidContact(i.contact)&&Length(Sub(i.worldHand,i.contact.point))<=s.reach+.002f&&
           Dot(Sub(i.worldHand,i.contact.point),i.contact.normal)>=-.002f){
            held_=true;anchor_=i.contact;target_=i.worldHand;velocity_={};start_=i.stamp;capturedRotation_=i.worldFromReference;
            out.held=out.acquired=out.command=true;
        }
    }
    previous_=i;have_=true;return out;
}
}
