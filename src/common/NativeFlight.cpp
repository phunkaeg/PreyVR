#include "preyvr/NativeFlight.h"
#include "preyvr/StereoCamera.h"
#include <cmath>

namespace preyvr::flight {
namespace {
bool Same(const Input& a,const Input& b) {
    return a.player==b.player&&a.epoch==b.epoch&&a.reference==b.reference&&a.generation==b.generation;
}
bool Good(const Quaternion& q) {
    const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(n)&&n>=.5f&&n<=2.f;
}
}
Output Controls::Update(const Input& i) {
    bool valid=i.valid&&i.active&&i.player&&!(i.reference&1)&&i.sequence&&i.stamp&&
        i.now>=i.stamp&&i.now-i.stamp<=100000000ull&&std::isfinite(i.x)&&std::isfinite(i.y);
    if(have_&&Same(i,previous_)) {
        if(i.sequence<previous_.sequence||i.stamp<previous_.stamp||i.now<previous_.now||
           (i.sequence==previous_.sequence&&(i.stamp!=previous_.stamp||i.up!=previous_.up||
            i.down!=previous_.down||i.boost!=previous_.boost||i.x!=previous_.x||i.y!=previous_.y))||
           (i.sequence!=previous_.sequence&&i.stamp<=previous_.stamp))valid=false;
        if(i.stamp>previous_.stamp&&i.stamp-previous_.stamp>100000000ull)ready_=false;
    } else ready_=false;
    if(!valid){ready_=false;have_=false;}
    else {
        if(!ready_&&!held_&&!i.up&&!i.down&&!i.boost&&std::hypot(i.x,i.y)<=.15f)ready_=true;
        previous_=i;have_=true;
    }
    const bool brake=i.up&&i.down;
    const auto desired=static_cast<std::uint8_t>(valid&&ready_?
        (brake?Brake:((i.up?Up:0)|(i.down?Down:0)|(i.boost?Boost:0))):0);
    Output out{static_cast<std::uint8_t>(desired&~held_),static_cast<std::uint8_t>(held_&~desired),
        valid&&ready_,valid&&ready_&&!brake,++serial_};
    if(!out.proposal)out.proposal=++serial_;
    pending_=out;return out;
}
void Controls::Commit(const Output& accepted) {
    if(!accepted.proposal||accepted.proposal!=pending_.proposal)return;
    const auto press=static_cast<std::uint8_t>(accepted.press&pending_.press);
    const auto release=static_cast<std::uint8_t>(accepted.release&pending_.release);
    held_=static_cast<std::uint8_t>((held_|press)&~release);
    pending_.press=static_cast<std::uint8_t>(pending_.press&~press);
    pending_.release=static_cast<std::uint8_t>(pending_.release&~release);
}
std::optional<Quaternion> Steering(float playYaw,const Pose& head,const PoseValidity& validity) {
    if(!std::isfinite(playYaw)||!IsPoseUsable(head,validity,100000000ull))return {};
    stereo::ReferenceFrame reference{};reference.yawRadians=playYaw;
    return stereo::EyePoseInWorld(reference,head).orientation;
}
bool OverrideRotation(NativeQuat& output,const Quaternion& desired) {
    if(!Good({output.x,output.y,output.z,output.w})||!Good(desired))return false;
    const auto q=Normalize(desired);output={q.w,q.x,q.y,q.z};return true;
}
}
