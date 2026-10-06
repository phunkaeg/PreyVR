#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace preyvr::options {
enum Setting : unsigned { Turn, HeadRelative, TurnSpeed, TwoHand, GripToggle,
    SupportSnap, Psychoscope, UiScale, UiMargin, UiCurve, UiGuide, Holsters, Wrist, WristSize,
    SceneReticle, ReticleFallback, Haptics, HapticStrength, PhysicalMelee, PhysicalContacts, SwingSpeed, ContactStrength,
    PsiTarget, Medkit, BeltHints, DebugOverlay, MuzzleAim, Count };
static_assert(Count<=32); // runtime dirty mailbox is a 32-bit mask
struct Spec { const char* key; const wchar_t* label; const wchar_t* help; int initial,min,max,step; };
const Spec& Describe(unsigned id);
bool Valid(unsigned id,int value);
using Values=std::array<int,Count>;
Values Defaults();
std::string Serialize(const Values&);
// Transactional parse: malformed/unknown fields refuse the whole file.
bool Parse(std::string_view text,Values& result);
std::wstring Display(unsigned id,int value);
constexpr unsigned Width=1400,Height=1100,Pages=8,Rows=4;
constexpr int CloseHit=Rows+Pages;
// -1 = recenter, -2 = empty, -3 = clear holsters, -4 = calibrate posture.
int Item(unsigned page,unsigned row);
const wchar_t* PageName(unsigned page);
struct Input {
    bool valid=false,modal=false,start=false,accept=false,cancel=false;
    bool previous=false,next=false,trigger=false;
    float x=0,y=0,dt=0;
    int hover=-1; // rows 0..Rows-1; tabs Rows..Rows+Pages-1; close CloseHit
    bool decrease=false;
    std::uint64_t epoch=0;
};
struct Change { int setting=-1; int direction=0; bool recenter=false,clearHolsters=false,calibrate=false; };
class Menu {
public:
    Change Update(const Input&);
    bool Open() const {return open_;}
    bool OwnsInput() const {return open_||quarantine_||pending_;}
    bool PauseTap() const {return pauseTap_;}
    void RequestOpen(){requested_=true;}
    void Close();
    unsigned page=0,row=0;
private:
    bool open_=false,quarantine_=false,armed_=false,requested_=false;
    bool startArmed_=false,startUsed_=false;
    bool startWas_=false,pending_=false,pauseTap_=false;
    bool accept_=false,cancel_=false,trigger_=false,previous_=false,next_=false;
    int stickX_=0,stickY_=0;
    float hold_=0,pendingTime_=0;
    std::uint64_t epoch_=0;
};
int Hit(float u,float v);
int Adjust(unsigned id,int value,int direction);
}
