#include "preyvr/PsiTargeting.h"
#include "preyvr/MedkitSlot.h"
#include <iostream>
#include <cstdlib>
#include <limits>
using namespace preyvr;
void Check(bool good,const char* why){if(!good){std::cerr<<why<<'\n';std::exit(1);}}
bool Near(float a,float b){return std::fabs(a-b)<.0001f;}
int main(){
 Pose head{{},{0,1.65f,0}},hand{{},{-.3f,1.2f,-.4f}};PoseValidity valid{true,true,true,true,0};
 const Vec3 centre{12,23,3};
 auto h=psi::BuildRay(psi::Mode::Head,0,centre,head,hand,valid);
 auto l=psi::BuildRay(psi::Mode::LeftController,0,centre,head,hand,valid);
 Check(h&&l&&Near(h->origin.x,12)&&Near(h->direction.y,1),"head starts at gameplay camera and points forward");
 Check(Near(l->origin.x,11.7f)&&Near(l->origin.y,23.4f)&&Near(l->origin.z,2.55f),"left ray uses head-relative controller position once");
 auto turned=psi::BuildRay(psi::Mode::LeftController,1.5707963f,centre,head,hand,valid);
 Check(turned&&Near(turned->direction.x,-1)&&Near(turned->origin.x,11.6f)&&Near(turned->origin.y,22.7f),"snap yaw rotates origin and direction together");
 hand.orientation={0,.70710678f,0,.70710678f};
 l=psi::BuildRay(psi::Mode::LeftController,0,centre,head,hand,valid);
 Check(l&&Near(l->direction.x,-1)&&Near(h->direction.y,1),"hand and head targeting independent");
 valid.orientationTracked=false;
 Check(!psi::BuildRay(psi::Mode::LeftController,0,centre,head,hand,valid),"lost tracking refuses aim");
 Check(!psi::BuildRay(psi::Mode::Native,0,centre,head,hand,valid),"native mode supplies no override");
 valid.orientationTracked=true;auto invalidHead=head;invalidHead.position.x=NAN;
 Check(!psi::BuildRay(psi::Mode::LeftController,0,centre,invalidHead,hand,valid),"invalid head-relative origin refuses aim");
 psi::Sample a{*h,100,200,300,2,1,2,4,1000000000};auto b=a;
 Check(psi::Castable(a,b,1050000000),"fresh preview can cast");
 for(int field=0;field<8;++field){b=a;switch(field){case 0:b.player++;break;case 1:b.component++;break;case 2:b.power++;break;case 3:b.selected++;break;case 4:b.epoch++;break;case 5:b.reference+=2;break;case 6:b.generation++;break;case 7:b.menu++;break;}
  Check(!psi::Castable(a,b,1050000000),"preview owner/state changes refuse cast");}
 Check(!psi::Castable(a,a,1200000000)&&!psi::Castable(a,a,900000000),"stale and future preview refused");
 psi::Context ctx{a,true,0};struct Buffer {psi::Ray ray;std::uint64_t guard;} buffer{{{7,8,9},{1,0,0}},0xABCDEF};
 Check(!psi::Override(&ctx,101+0x40,&buffer.ray)&&buffer.ray.origin.x==7,"other receiver unaffected");
 Check(psi::Override(&ctx,100+0x40,&buffer.ray)&&buffer.guard==0xABCDEF&&ctx.reads==1,"only 24 getter output bytes replaced");
 Check(!psi::Override(nullptr,100+0x40,&buffer.ray),"getter outside psi scope unaffected");
 psi::Trigger trigger;
 Check(trigger.Update(true,true)==psi::TriggerEvent::None,"held trigger on entry cannot start");
 trigger.Update(false,true);Check(trigger.Update(true,true)==psi::TriggerEvent::Begin,"fresh press starts once");
 Check(trigger.Update(true,true)==psi::TriggerEvent::None&&trigger.Update(false,true)==psi::TriggerEvent::Cast,"hold previews, release casts once");
 trigger.Update(true,true);Check(trigger.Update(true,false)==psi::TriggerEvent::Cancel,"modal/tracking cancels pending cast");
 Check(trigger.Update(true,true)==psi::TriggerEvent::None&&trigger.Update(false,true)==psi::TriggerEvent::None,"canceled held input cannot cast on release");
 equipment::MedkitSlot slot;hand.orientation={};hand.position={-.25f,1.f,.02f};
 auto tick=[&](bool grip,bool fire,bool ok=true,float dt=.01f){return slot.Update(head,hand.position,grip,fire,ok,dt);};
 tick(false,false);Check(!tick(true,false)&&slot.OwnsGrip(),"fresh hip grab claims slot without consuming");
 hand.position={-.3f,1.3f,-.4f};Check(tick(true,true),"trigger while holding consumes once after leaving belt");
 for(int i=0;i<5;++i){Check(!tick(true,false)&&!tick(true,true),"one transaction per grip, even repeated trigger pulls");}
 tick(false,false);hand.position={-.25f,1.f,.02f};tick(true,true);
 Check(!tick(true,true),"held trigger cannot consume on acquisition");tick(true,false);Check(tick(true,true),"neutral trigger rearms deliberate confirmation");
 tick(true,false,false);Check(!slot.OwnsGrip(),"tracking/menu invalidation releases slot");
 tick(true,false);Check(!tick(true,true),"held grip after cancellation cannot reacquire");
 tick(false,false);hand.position={-.5f,1.3f,-.4f};tick(true,false);hand.position={-.25f,1.f,.02f};
 Check(!tick(true,true)&&!slot.OwnsGrip(),"drifting into belt with grip held cannot consume");
 std::cout<<"Psi ray, preview provenance, trigger cancellation and single-use medkit slot passed\n";
}
