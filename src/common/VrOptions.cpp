#include "preyvr/VrOptions.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
namespace preyvr::options {
namespace {
constexpr Spec specs[]={
 {"turn",L"TURNING",L"Smooth turning or fixed angular steps.",45,0,90,15},
 {"head_relative",L"MOVEMENT DIRECTION",L"Follow your view, or the character's forward direction.",1,0,1,1},
 {"turn_speed",L"SMOOTH TURN SPEED",L"Scales the game's turn rate. Applies in Smooth mode.",100,10,200,25},
 {"two_hand",L"TWO-HAND AIM",L"Grip near a long weapon's foregrip to steady and aim it.",1,0,1,1},
 {"grip_toggle",L"FOREGRIP INPUT",L"Hold the grip, or squeeze once to attach and again to release.",0,0,1,1},
 {"support_snap",L"SUPPORT HAND",L"Snap the displayed hand to the weapon, or follow your controller.",1,0,1,1},
 {"psychoscope",L"PSYCHOSCOPE GESTURE",L"Left grip at forehead: pull down or lift up. Native unlock rules apply.",0,0,1,1},
 {"ui_scale",L"INTERFACE SIZE",L"Scales game panels and HUD within the visible-area limit.",100,20,200,10},
 {"ui_margin",L"VISIBLE-AREA LIMIT",L"Larger values approach the lens edges. Tune while wearing the headset.",72,30,100,5},
 {"ui_curve",L"PANEL CURVATURE",L"Curves supported mono menus. Experimental stereo PDA stays planar.",35,0,60,5},
 {"ui_guide",L"CONTROLS GUIDE",L"Shows a help card below native menus; uses some panel space.",0,0,1,1},
 {"holsters",L"BODY HOLSTERS",L"Right grip at right hip or left chest: store current weapon, then draw/stow. Session only.",0,0,1,1},
 {"wrist",L"WRIST STATUS",L"Turn your left palm up and look at your wrist for health, psi and suit integrity.",0,0,1,1},
 {"wrist_size",L"WRIST DISPLAY SIZE",L"Adjust the status card size. Hidden in menus and during two-hand aiming.",100,80,140,10}
};
}
const Spec& Describe(unsigned id){return specs[id<Count?id:0];}
bool Valid(unsigned id,int v){
 if(id>=Count||v<specs[id].min||v>specs[id].max)return false;
 return id!=Turn||v==0||v>=15;
}
Values Defaults(){Values v{};for(unsigned i=0;i<Count;++i)v[i]=specs[i].initial;return v;}
std::string Serialize(const Values& v){
 std::string s="version=1\n";for(unsigned i=0;i<Count;++i)s+=std::string(specs[i].key)+"="+std::to_string(v[i])+"\n";return s;
}
bool Parse(std::string_view text,Values& result){
 if(text.size()>8192)return false;
 Values parsed=Defaults();std::array<bool,Count> seen{};bool version=false;
 std::istringstream in{std::string(text)};std::string line;
 while(std::getline(in,line)){
  if(!line.empty()&&line.back()=='\r')line.pop_back();
  if(line.empty())continue;
  const auto sep=line.find('=');if(sep==std::string::npos)return false;
  int value=0;const auto first=line.data()+sep+1,last=line.data()+line.size();
  const auto [end,err]=std::from_chars(first,last,value);
  if(err!=std::errc{}||end!=last)return false;
  const auto key=line.substr(0,sep);
  if(key=="version"){if(version||value!=1)return false;version=true;continue;}
  unsigned i=0;for(;i<Count&&key!=specs[i].key;++i){}
  if(i==Count||seen[i]||!Valid(i,value))return false;
  parsed[i]=value;seen[i]=true;
 }
 if(!version)return false;
 // Partial files preserve explicit defaults; future versions must bump version.
 result=parsed;return true;
}
int Item(unsigned page,unsigned row){
 constexpr int items[Pages][Rows]={{Turn,HeadRelative,TurnSpeed,-1},{TwoHand,GripToggle,SupportSnap,Psychoscope},{UiScale,UiMargin,UiCurve,UiGuide},{Holsters,Wrist,WristSize,-3}};
 return page<Pages&&row<Rows?items[page][row]:-2;
}
const wchar_t* PageName(unsigned p){constexpr const wchar_t* names[]={L"COMFORT",L"HANDS",L"INTERFACE",L"EQUIPMENT"};return names[p%Pages];}
std::wstring Display(unsigned id,int v){
 if(id==Turn)return v?std::to_wstring(v)+L" DEGREES":L"SMOOTH";
 if(id==HeadRelative)return v?L"HEAD RELATIVE":L"BODY RELATIVE";
 if(id==GripToggle)return v?L"TOGGLE":L"HOLD";
 if(id==SupportSnap)return v?L"SNAPPED":L"FREE";
 if(id==UiCurve)return v?std::to_wstring(v)+L" DEGREES":L"FLAT";
 if(id==TurnSpeed||id==UiScale||id==UiMargin||id==WristSize)return std::to_wstring(v)+L"%";
 return v?L"ON":L"OFF";
}
int Adjust(unsigned id,int v,int direction){
 if(id>=Count||!direction)return v;
 if(id==Turn){
  constexpr int choices[]={0,15,30,45,60,90};
  if(direction>0){for(int c:choices)if(c>v)return c;return 0;}
  for(int n=5;n>=0;--n)if(choices[n]<v)return choices[n];return 90;
 }
 const auto& s=specs[id];if(s.max==1)return v?0:1;
 return std::clamp(v+(direction>0?s.step:-s.step),s.min,s.max);
}
int Hit(float u,float v){
 if(!std::isfinite(u)||!std::isfinite(v)||u<.04f||u>.96f||v<0||v>1)return -1;
 const float y=v*Height;
 if(y>=174&&y<246)return Rows+std::min(static_cast<int>(Pages-1),static_cast<int>((u-.04f)/.92f*Pages));
 if(y>=280&&y<744)return std::min(3,static_cast<int>((y-280)/116));
 if(y>=980&&y<1060)return CloseHit;
 return -1;
}
void Menu::Close(){open_=false;quarantine_=true;armed_=false;requested_=false;pending_=false;}
Change Menu::Update(const Input& i){
 Change out;
 pauseTap_=false;
 const bool valid=i.valid&&std::isfinite(i.x)&&std::isfinite(i.y)&&std::isfinite(i.dt)&&i.dt>0&&i.dt<=.2f;
 const bool neutral=!i.start&&!i.accept&&!i.cancel&&!i.previous&&!i.next&&!i.trigger&&std::fabs(i.x)<.25f&&std::fabs(i.y)<.25f;
 if(!valid||i.epoch!=epoch_){
  epoch_=i.epoch;Close();startArmed_=false;startUsed_=false;startWas_=false;hold_=0;return out;
 }
 if(!i.start){
  if(startWas_&&startArmed_&&!startUsed_&&!open_&&!quarantine_)pauseTap_=true;
  hold_=0;startArmed_=true;startUsed_=false;
 }
 else if(startArmed_&&!startUsed_)hold_+=i.dt;
 startWas_=i.start;
 if(pending_){pendingTime_+=i.dt;if(pendingTime_>2.f)Close();}
 if(!open_&&!i.modal&&hold_>=.65f&&!startUsed_&&!quarantine_){
  pauseTap_=true;pending_=true;pendingTime_=0;startUsed_=true;
 }
 if(!open_&&i.modal&&((hold_>=.65f&&!startUsed_)||requested_||pending_)){
  open_=true;quarantine_=true;armed_=false;requested_=false;startUsed_=true;
  pending_=false;
 }
 if(open_&&!i.modal)Close();
 if(!open_){if(neutral)quarantine_=false;return out;}
 if(!armed_){if(neutral){armed_=true;accept_=cancel_=trigger_=previous_=next_=false;stickX_=stickY_=0;}return out;}
 const bool accept=i.accept&&!accept_,cancel=i.cancel&&!cancel_,click=i.trigger&&!trigger_;
 const bool prev=i.previous&&!previous_,next=i.next&&!next_;
 accept_=i.accept;cancel_=i.cancel;trigger_=i.trigger;previous_=i.previous;next_=i.next;
 if(cancel||(click&&i.hover==CloseHit)){Close();return out;}
 if(prev!=next){page=(page+(next?1:Pages-1))%Pages;row=0;}
 if(click&&i.hover>=static_cast<int>(Rows)&&i.hover<CloseHit){page=static_cast<unsigned>(i.hover-Rows);row=0;return out;}
 const int x=i.x>.65f?1:(i.x<-.65f?-1:0),y=i.y>.65f?1:(i.y<-.65f?-1:0);
 const int moveY=y&&stickY_==0?y:0,moveX=x&&stickX_==0?x:0;
 if(std::fabs(i.x)<.25f)stickX_=0;else if(x)stickX_=x;
 if(std::fabs(i.y)<.25f)stickY_=0;else if(y)stickY_=y;
 if(moveY)row=(row+(moveY>0?Rows-1:1))%Rows;
 if(click&&i.hover>=0&&i.hover<4)row=static_cast<unsigned>(i.hover);
 if(moveX||accept||(click&&i.hover>=0&&i.hover<4)){
  out.setting=Item(page,row);out.direction=moveX?moveX:((click&&i.decrease)?-1:1);out.recenter=out.setting==-1;out.clearHolsters=out.setting==-3;
 }
 return out;
}
}
