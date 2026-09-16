#include "preyvr/TwoHandedAim.h"
#include "preyvr/MotionController.h"
#include "preyvr/WeaponRigAlignment.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace preyvr;
using namespace preyvr::twohand;
void Check(bool value,const char* message) { if(!value) { std::cerr<<message<<'\n';std::exit(1); } }
bool Near(Vec3 a,Vec3 b,float epsilon=.002f) { return std::fabs(a.x-b.x)<epsilon&&std::fabs(a.y-b.y)<epsilon&&std::fabs(a.z-b.z)<epsilon; }
Input Base() { Input i{};i.owner=1;i.epoch=1;i.reference=2;i.usable=true;i.dt=.01f;
    i.region.start={0,.25f,0};i.region.end={0,.45f,0};i.support.position={0,.35f,0};return i; }
Output Hold(Solver& s,Input& i) { s.Update(i);i.squeeze=1; Output out{};for(int n=0;n<60;++n)out=s.Update(i);return out; }
int main() {
    auto i=Base();Solver s;auto out=Hold(s,i);
    Check(out.held&&out.blend>.99f,"grip enters in region");
    Check(std::fabs(out.socket.y-.35f)<.001f,"nearest point locks within capsule");
    i.support.position={.35f,0,0};out=s.Update(i);
    Check(out.held,"leaving acquisition region does not release latch");
    Check(Near(Rotate(out.orientation,{0,1,0}),{1,0,0}),"raw offhand steers barrel");
    // Feed solved orientation through the real wrist alignment contract.
    weaponrig::Basis basis{};basis.source=weaponrig::Source::jointBind;
    basis.weaponInWrist=Normalize({.2f,.1f,.3f,.9f});basis.barrelInWeapon=Normalize({.1f,.3f,.1f,.8f});
    const Quaternion character=Normalize({.1f,.2f,.4f,.9f});Quaternion wrist{};
    Check(weaponrig::SolveWrist(character,out.orientation,basis,wrist),"wrist solve accepts two-hand aim");
    Check(Near(Rotate(Multiply(Multiply(Multiply(character,wrist),basis.weaponInWrist),basis.barrelInWeapon),{0,1,0}),
               Rotate(out.orientation,{0,1,0})),"model barrel and firing ray share solved direction");
    i.squeeze=0;out=s.Update(i);Check(!out.held&&out.blend>0,"release blends instead of snapping");
    for(int n=0;n<60;++n)out=s.Update(i);
    Check(Near(Rotate(out.orientation,{0,1,0}),{0,1,0}),"release restores one-hand aim");
    i=Base();s={};i.support.position={1,.35f,0};out=Hold(s,i);Check(!out.held,"empty-air squeeze cannot acquire");
    i.support.position={0,.35f,0};Check(!s.Update(i).held,"held failed squeeze cannot acquire by drifting in");
    i.squeeze=0;s.Update(i);i.squeeze=1;Check(s.Update(i).held,"fresh squeeze can acquire");
    i.owner=2;Check(!s.Update(i).held,"weapon switch clears hold");Check(!s.Update(i).held,"new weapon requires release");
    i.squeeze=0;s.Update(i);i.squeeze=.7f;Check(s.Update(i).held,"new owner reacquires on squeeze");
    i.squeeze=.5f;Check(s.Update(i).held,"squeeze hysteresis holds at half travel");
    i.reference+=2;Check(!s.Update(i).held,"recenter invalidates hold");
    i=Base();s={};Hold(s,i);i.usable=false;out=s.Update(i);Check(!out.held&&out.blend==0,"tracking/modal loss resets immediately");
    i.usable=true;Check(!s.Update(i).held,"held reacquired controller cannot auto-grab");
    i=Base();s={};Hold(s,i);i.epoch++;Check(!s.Update(i).held,"session epoch invalidates hold");
    i=Base();s={};Hold(s,i);i.support.position={0,0,.35f};out=s.Update(i);
    Check(out.held&&Near(Rotate(out.orientation,{0,1,0}),{0,0,1}),"vertical aiming has no up-vector singularity");
    i.support.position={0,-.35f,0};out=s.Update(i);Check(Near(Rotate(out.orientation,{0,1,0}),{0,-1,0}),"180 degree turn is finite");
    i.support.position={0,.01f,0};out=s.Update(i);Check(!out.held&&out.blend==0,"near-coincident hands cannot spin weapon");
    i=Base();s={};i.region.start=i.region.end={0,.05f,0};i.support.position={0,.05f,0};Check(!Hold(s,i).held,"short adjacent grips are excluded");
    i=Base();s={};i.visualPrimaryOffset={.1f,0,0};i.support.position={.1f,.35f,0};out=Hold(s,i);
    Check(out.held&&Near(Rotate(out.orientation,{0,1,0}),{0,1,0}),"acquisition and steering account for visible wrist reach compression");
    i=Base();s={};i.region.start=i.region.end={.06f,.35f,-.03f};i.support.position=i.region.start;out=Hold(s,i);
    Check(out.held&&Near(Rotate(out.orientation,{0,1,0}),{0,1,0}),"authored off-axis socket does not introduce aim offset on grab");
    i=Base();s={};Hold(s,i);i.squeeze=std::numeric_limits<float>::quiet_NaN();Check(!s.Update(i).held,"NaN squeeze refuses");
    i=Base();s={};Hold(s,i);i.support.position.x=std::numeric_limits<float>::infinity();Check(!s.Update(i).held,"nonfinite tracking refuses");
    i=Base();s={};Hold(s,i);i.dt=1;Check(!s.Update(i).held,"long stall resets smoothing");
    std::cout<<"Two-handed aim geometry, lifecycle and barrel integration passed\n";
}
