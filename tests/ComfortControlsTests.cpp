#include "preyvr/ComfortControls.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace preyvr;
using namespace preyvr::comfort;
void Require(bool b,const char* why){if(!b){std::cerr<<why<<'\n';std::exit(1);}}
bool Near(float a,float b){return std::abs(a-b)<.0001f;}
bool Near(Vec3 a,Vec3 b){return Near(a.x,b.x)&&Near(a.y,b.y)&&Near(a.z,b.z);}
Pose World(Vec3 origin,float yaw,Pose head){
    head.position={head.position.x-origin.x,head.position.y-origin.y,head.position.z-origin.z};
    stereo::ReferenceFrame ref{};ref.yawRadians=-yaw;
    return stereo::EyePoseInWorld(ref,head);
}
int main(){
    SnapLatch snap;
    Require(snap.Update(1,0,true)==0,"held on activation must not turn");
    Require(snap.Update(0,0,true)==0,"centre arms");
    Require(snap.Update(1,0,true)==1,"right turns once");
    Require(snap.Update(1,0,true)==0,"held does not repeat");
    Require(snap.Update(.4f,0,true)==0,"hysteresis does not rearm early");
    Require(snap.Update(-1,0,true)==0,"crossing without sampled centre does not repeat");
    snap.Update(0,0,true);Require(snap.Update(-1,0,true)==-1,"left after centre");
    snap.Update(0,0,true);Require(snap.Update(.8f,1,true)==0,"vertical dominant ignored");
    snap.Update(0,0,false);Require(snap.Update(1,0,true)==0,"menu blocks and requires fresh neutral");
    snap.Update(0,0,true);snap.Update(std::numeric_limits<float>::quiet_NaN(),0,true);
    Require(snap.Update(1,0,true)==0,"tracking error disarms");
    constexpr float pi=3.14159265358979323846f;
    auto v=HeadRelativeStick({0,1},pi/2,1,1);
    Require(Near(v.x,-1)&&Near(v.y,0),"head left rotates forward into body left");
    for(float yaw:{-2.f,-.6f,0.f,.7f,2.f})for(Vec2 raw: {Vec2{0,1},Vec2{.6f,.8f},Vec2{-.4f,-.7f}}){
        auto out=HeadRelativeStick(raw,yaw,.8f,.5f);
        Vec2 native{out.x*.8f,out.y*(out.y<0?.5f:1.f)};
        Vec2 desired{std::cos(yaw)*raw.x-std::sin(yaw)*raw.y,std::sin(yaw)*raw.x+std::cos(yaw)*raw.y};
        Require(std::abs(native.x*desired.y-native.y*desired.x)<.0001f,"optional scale compensation preserves direction");
        Require(native.x*desired.x+native.y*desired.y>0,"movement never reverses");
        Require(std::hypot(out.x,out.y)<=1.0001f,"axis magnitude bounded");
    }
    Vec3 origin{.2f,1.7f,-.4f};Pose head{{},{.8f,1.2f,-.9f}};
    for(float yaw:{-1.f,0.f,1.3f})for(int step:{-1,1}){
        const float delta=static_cast<float>(step)*pi/4;
        auto before=World(origin,yaw,head);
        auto after=World(TurnOriginAboutHead(origin,head.position,delta),yaw+delta,head);
        Require(Near(before.position,after.position),"snap pivots around head without teleporting");
        Require(Near(TurnOriginAboutHead(origin,head.position,delta).y,origin.y),"snap preserves height baseline");
        auto forward=Rotate(after.orientation,{0,1,0});
        if(yaw==0) Require(step>0?forward.x>0:forward.x<0,"right stick turns world view right");
    }
    Require(Near(RecenterOrigin(head.position,origin,true).y,1.7f),"crouched recenter preserves standing height");
    Require(Near(RecenterOrigin(head.position,origin,false).y,1.2f),"explicit calibration changes height");
    for(float changeYaw:{-.8f,.6f}){
        Pose previousFromNew{{0,std::sin(changeYaw/2),0,std::cos(changeYaw/2)},{.5f,.3f,-.2f}};
        Pose inverse{stereo::Conjugate(previousFromNew.orientation),{}};
        inverse.position=Rotate(inverse.orientation,{-previousFromNew.position.x,-previousFromNew.position.y,-previousFromNew.position.z});
        auto headNew=Compose(inverse,head);auto newOrigin=origin;float oldYaw=.4f,newYaw=oldYaw;
        Require(RebaseReference(newOrigin,newYaw,previousFromNew),"valid runtime rebase");
        const auto before=World(origin,oldYaw,head),after=World(newOrigin,newYaw,headNew);
        Require(Near(before.position,after.position),"runtime rebase preserves world head position");
        Require(Near(Rotate(before.orientation,{0,1,0}),Rotate(after.orientation,{0,1,0})),"runtime rebase preserves world facing");
    }
    float yaw=0;Pose invalid{};invalid.position.x=std::numeric_limits<float>::infinity();
    Require(!RebaseReference(origin,yaw,invalid),"invalid event refused");
    invalid={};invalid.orientation={.5f,0,0,.8660254f};
    Require(!RebaseReference(origin,yaw,invalid),"tilted space cannot fit yaw-only mapping");
    std::cout<<"Comfort controls passed\n";
}
