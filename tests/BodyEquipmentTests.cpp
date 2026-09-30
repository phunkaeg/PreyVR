#include "preyvr/BodyEquipment.h"
#include <iostream>
#include <cstdlib>
using namespace preyvr;
using namespace preyvr::equipment;
void Check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
 Pose head{};head.position={0,1.65f,0};
 auto world=[&](Vec3 p){auto v=Rotate(head.orientation,p);return Vec3{v.x+head.position.x,v.y+head.position.y,v.z+head.position.z};};
 HolsterGesture g;
 auto tick=[&](Vec3 p,bool grip,bool valid=true,float dt=.01f){return g.Update(head,p,grip,valid,dt);};
 const auto hip=world(Zones[0]),chest=world(Zones[1]);
 Check(tick(hip,true)==-1,"held on entry cannot stow");
 tick(hip,false);Check(tick(hip,true)==0&&g.OwnsGrip(),"fresh hip grip claims use/reload");
 Check(tick(chest,true)==-1&&g.OwnsGrip(),"held grip cannot activate second slot or leak use");
 tick(chest,false);Check(tick(chest,true)==1,"fresh chest grip selects distinct slot");
 tick(hip,false);Check(tick({0,1.5f,-.7f},true)==-1&&!g.OwnsGrip(),"ordinary reload grip untouched");
 Check(tick(hip,true)==-1,"drift into holster with held grip ignored");
 tick(hip,false);tick(hip,true);tick(hip,true,false);
 Check(tick(hip,true)==-1&&!g.OwnsGrip(),"tracking/modal cancellation needs release");
 tick(hip,false);Check(tick(hip,true,true,.3f)==-1,"frame stall disarms");
 Check(tick(hip,true)==-1,"stall cannot turn held grip into fresh grab");
 g.Reset();head.orientation={0,.70710678f,0,.70710678f};
 auto turnedHip=world(Zones[0]);tick(turnedHip,false);Check(tick(turnedHip,true)==0,"world yaw rotates body zones");
 head.orientation={0,0,0,1};g.Reset();tick(hip,false);
 // A small glance changes head yaw but does not move the reached hip zone.
 head.orientation={0,std::sin(.3f),0,std::cos(.3f)};
 Check(tick(hip,true)==0,"head glance does not drag reached zone");
 Check(DisplayValue(753.f,.1f)==76&&DisplayValue(0)==0,"HUD rounding and real zero preserved");
 Check(DisplayValue(NAN)==-1&&DisplayValue(-1)==-1,"unknown value distinct from zero");
 Pose eye{},card{};card.position={0,0,-.5f};
 Check(WristVisible(eye,card,false),"front face in gaze visible");
 card.orientation={0,1,0,0};Check(!WristVisible(eye,card,true),"back of wrist hidden");
 card={};card.position={0,0,-.1f};Check(!WristVisible(eye,card,true),"too near face hidden");
 card.position={.7f,0,-.1f};Check(!WristVisible(eye,card,false),"outside inspection gaze hidden");
 Pose grip{};grip.position={-.2f,-.35f,-.35f};const auto panel=WristPose(grip);
 Check(std::fabs(panel.position.z+.24f)<.0001f&&std::fabs(panel.position.y+.305f)<.0001f,"wrist anchor follows grip toward arm");
 std::cout<<"Holster neutral/ownership, torso geometry, wrist visibility and HUD conversion passed\n";
}
