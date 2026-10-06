#include "preyvr/VrOptions.h"
#include "preyvr/PsychoscopeGesture.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace preyvr;
using namespace preyvr::options;
void Check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
Input Base(){Input i;i.valid=i.modal=true;i.dt=.01f;i.epoch=1;return i;}
void Ready(Menu& m,Input& i){m.Update(i);m.Update(i);}
void Open(Menu& m,Input& i){Ready(m,i);m.RequestOpen();m.Update(i);m.Update(i);Check(m.Open(),"open on modal");}
void MenuChecks(){
 Input i=Base();Menu m;Ready(m,i);
 i.start=true;m.Update(i);Check(!m.PauseTap(),"pause deferred until tap or hold");
 i.start=false;m.Update(i);Check(m.PauseTap()&&!m.Open(),"short tap goes to native pause once");
 m.Update(i);Check(!m.PauseTap(),"no repeated pause");
 i.start=true;for(int n=0;n<70;++n)m.Update(i);
 Check(m.Open()&&m.OwnsInput()&&!m.PauseTap(),"long hold on existing menu opens options without closing native menu");
 i.accept=true;Check(m.Update(i).setting==-1,"opening held accept cannot edit");
 i.start=i.accept=false;m.Update(i);
 i.accept=true;Check(m.Update(i).setting==Turn,"fresh accept edits selected");
 Check(m.Update(i).setting==-1,"held accept does not repeat");
 i.accept=false;i.next=true;m.Update(i);Check(m.page==1,"next tab");
 m.Update(i);Check(m.page==1,"held tab no repeat");
 i.next=false;i.y=-1;m.Update(i);Check(m.row==1,"down selects next");
 m.Update(i);Check(m.row==1,"held stick no repeat");
 i.y=0;m.Update(i);i.y=-1;m.Update(i);Check(m.row==2,"neutral rearm stick");
 i.y=0;i.x=1;auto change=m.Update(i);Check(change.setting==SupportSnap&&change.direction==1,"selected grip preference");
 i.x=0;i.trigger=true;i.hover=4;m.Update(i);Check(m.page==0,"ray tab click");
 i.trigger=false;m.Update(i);i.trigger=true;i.hover=3;Check(m.Update(i).recenter,"ray recenter action");
 i.trigger=false;i.cancel=true;m.Update(i);Check(!m.Open()&&m.OwnsInput(),"closing press remains quarantined");
 m.Update(i);Check(m.OwnsInput(),"held close never leaks");
 i.cancel=false;m.Update(i);Check(!m.OwnsInput(),"neutral gives input back");
 m={};i=Base();i.modal=false;Ready(m,i);i.start=true;
 int pauses=0;for(int n=0;n<70;++n){m.Update(i);pauses+=m.PauseTap();}
 Check(pauses==1&&!m.Open()&&m.OwnsInput(),"long hold from play requests one native pause");
 i.modal=true;m.Update(i);Check(m.Open(),"wait for observed native menu before showing options");
 m={};i=Base();i.modal=false;Ready(m,i);i.start=true;
 for(int n=0;n<300;++n)m.Update(i);i.start=false;m.Update(i);i.modal=true;m.Update(i);
 Check(!m.Open(),"failed pause request expires instead of surprising later");
 m={};i=Base();Open(m,i);i.modal=false;m.Update(i);Check(!m.Open(),"native modal ends closes overlay");
 m={};i=Base();Open(m,i);i.valid=false;m.Update(i);Check(!m.Open(),"tracking loss closes");
 i.valid=true;i.accept=true;m.Update(i);Check(!m.Open(),"regained held input cannot open");
 m={};i=Base();Open(m,i);i.epoch++;m.Update(i);Check(!m.Open(),"session transition closes");
 Check(Hit(.5f,310.f/Height)==0&&Hit(.5f,1010.f/Height)==CloseHit,"shared row and close hit layout");
 for(unsigned p=0;p<Pages;++p)Check(Hit(.04f+.92f*(p+.5f)/Pages,200.f/Height)==static_cast<int>(Rows+p),"each visible tab selects itself");
 Check(Hit(-.1f,.4f)==-1&&Hit(.5f,.82f)==-1&&Hit(NAN,.4f)==-1,"outside and help text not actionable");
}
void SettingsChecks(){
 auto v=Defaults(),parsed=v;
 v[Psychoscope]=1;v[GripToggle]=1;v[UiMargin]=87;
 Check(Parse(Serialize(v),parsed)&&parsed==v,"settings roundtrip");
 const auto original=parsed;
 for(auto s:{"version=2\n","version=1\nturn=10\n","version=1\npsychoscope=2\n","version=1\nui_scale=500\n","version=1\nx=3\n","version=1\nturn=45oops\n","version=1\nturn=45\nturn=30\n"}){
  Check(!Parse(s,parsed)&&parsed==original,"invalid file has no partial effects");
 }
 Check(Adjust(Turn,60,1)==90&&Adjust(Turn,90,1)==0,"turn choices skip unsupported 75");
 Check(Valid(Turn,75)&&Adjust(Turn,75,1)==90&&Adjust(Turn,75,-1)==60,"custom command angle displays and adjusts correctly");
 Check(Adjust(UiScale,200,1)==200&&Adjust(UiScale,20,-1)==20,"scale limits");
 Check(Defaults()[Psychoscope]==0,"gesture opt in");
 Check(Defaults()[Holsters]==0&&Defaults()[Wrist]==0,"equipment features opt in");
 Check(Parse("version=1\nturn=45\n",parsed)&&parsed[Holsters]==0&&parsed[WristSize]==100,"old settings upgrade safely");
 Check(Item(3,0)==Holsters&&Item(3,1)==Wrist&&Item(3,3)==-3,"equipment page layout");
 Check(Defaults()[PsiTarget]==0&&Defaults()[Medkit]==0,"abilities preserve original behavior by default");
 Check(Valid(PsiTarget,2)&&!Valid(PsiTarget,3)&&!Valid(Medkit,2),"ability ranges");
 Menu abilities;Input ai=Base();Open(abilities,ai);abilities.page=6;abilities.row=1;
 ai.y=-1;abilities.Update(ai);Check(abilities.row==2,"body slot feedback is an ability setting");
 ai.y=0;abilities.Update(ai);ai.y=-1;abilities.Update(ai);Check(abilities.row==0,"navigation skips empty ability rows");
 ai.y=0;abilities.Update(ai);ai.y=1;abilities.Update(ai);Check(abilities.row==2,"reverse navigation skips empty rows");
 ai.y=0;ai.trigger=true;ai.hover=3;Check(abilities.Update(ai).setting==-1&&abilities.row==2,"empty row is not interactive");
 Menu menu;Input input=Base();Open(menu,input);input.trigger=true;input.hover=7;menu.Update(input);
 Check(menu.Open()&&menu.page==3,"fourth tab is not mistaken for close");
 input.trigger=false;menu.Update(input);input.trigger=true;input.hover=3;
 Check(menu.Update(input).clearHolsters,"clear assignments distinct from recenter");
 Menu setup;Input si=Base();Open(setup,si);setup.page=7;
 si.accept=true;const auto calibration=setup.Update(si);
 Check(calibration.calibrate&&!calibration.recenter&&!calibration.clearHolsters,"posture calibration is a distinct one-shot action");
 Check(!setup.Update(si).calibrate,"held calibration button never repeats");
 si.accept=false;setup.Update(si);si.y=-1;setup.Update(si);si.y=0;setup.Update(si);si.accept=true;
 Check(setup.Update(si).recenter,"setup retains reset without height calibration");
 Check(Item(7,2)==DebugOverlay&&Defaults()[DebugOverlay]==0,"debug overlay is an opt-in setup row");
 Check(Parse("version=1\ndebug_overlay=1\n",parsed)&&parsed[DebugOverlay]==1&&!Valid(DebugOverlay,2),"debug overlay persists as on/off");
}
void GestureChecks(){
 PsychoscopeGesture g;const Vec3 high{-.12f,.18f,-.12f},low{-.12f,-.02f,-.12f};
 auto tick=[&](Vec3 p,bool grip,bool usable=true,float dt=.02f,std::uint64_t epoch=1){return g.Update(p,grip,usable,dt,epoch);};
 tick(high,false);tick(high,false);tick(high,true);
 for(int n=0;n<8;++n)Check(!tick(high,true),"grip near head without travel never toggles");
 Check(tick(low,true),"downward pull toggles once");
 for(int n=0;n<10;++n)Check(!tick(low,true),"held stroke cannot retrigger");
 tick(low,false);tick(low,true);for(int n=0;n<7;++n)tick(low,true);
 Check(tick(high,true),"upward lift toggles");
 g={};tick(high,true);for(int n=0;n<12;++n)Check(!tick(low,true),"held on entry disarmed");
 tick(high,false);tick(high,true);tick(high,true,false);tick(high,true);
 for(int n=0;n<10;++n)Check(!tick(low,true),"tracking loss needs release");
 tick(high,false);tick(high,true);tick(low,true,true,.4f);
 for(int n=0;n<10;++n)Check(!tick(low,true),"stall invalidates stroke");
 tick(high,false);tick(high,true);tick(low,true,true,.02f,2);
 for(int n=0;n<10;++n)Check(!tick(low,true,true,.02f,2),"epoch transition invalidates stroke");
 g={};tick(high,false);tick(high,false);tick(high,true);
 Check(!tick({NAN,0,0},true),"nonfinite pose refuses");
}
int main(){MenuChecks();SettingsChecks();GestureChecks();std::cout<<"VR options ownership, persistence and gesture contracts passed\n";}
