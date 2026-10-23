// Actual adapter, synthetic native/XR boundaries. No game is started.
#include "../src/dll/BodyFollow.cpp"
#include <iostream>
#include <stdexcept>
namespace {
using namespace preyvr; using namespace preyvr::dll;
bool room = true, tracked = true;
std::uint64_t reference = 2;
TrackingFrame sample{};
alignas(16) unsigned char actorA[0x1900]{}, actorB[0x1900]{};
unsigned char* actor = actorA;
std::uint8_t* __fastcall GetActor() { return actor; }
void Check(bool value, const char* why) { if (!value) { throw std::runtime_error(why); } }
void Stance(int value) { std::memcpy(actor + kPlayerStance, &value, sizeof(value)); }
void Fresh() { sample.publishedNs = MonotonicNanoseconds(); }
void Reset()
{
    ResetFollowState(); gFollowContext = {}; gCrouchOn = true; gFollowActive = true;
    actor = actorA; Stance(kStanceStand); room = tracked = true; reference = 2;
    sample = {}; sample.epoch = 1; sample.head.position = {0, 1.7f, 0};
    sample.headValidity = {true, true, true, true, 0}; Fresh();
    gBase = reinterpret_cast<std::uintptr_t>(&GetActor) - kGetPlayerRva; gInstalled = true;
}
void Eye(float z) { Fresh(); NoteGameView({0, 0, z}, Quaternion{}); }
void Baseline(float z = 1.7f) { for (int i = 0; i < 20; ++i) { Eye(z); } }
void Crouch(float drop)
{
    Fresh(); BindFollowContext(actor, sample, reference);
    UpdateCrouch(actor, {0, 1.7f - .075f - drop, 0}, {0, 1.7f, 0}, kStateGround);
}
}
namespace preyvr::lifecycle { void Log(std::string_view) {} }
namespace preyvr::dll {
bool EnsureMinHook() { return false; }
bool HudGameplayInputAllowed() { return true; }
int MoveLaneCinematicGate() { return 0; }
int MoveLaneAxisMilli(unsigned) { return 0; }
bool TryGetTrackingFrame(TrackingFrame& f) { f = sample; return tracked; }
bool TryGetGameplayPoseFrame(GameplayPoseFrame& f, bool)
{
    f = {}; f.player = reinterpret_cast<std::uintptr_t>(actor);
    f.referenceGeneration = reference; f.tracking = sample; return tracked;
}
unsigned long long HeadTrackingReferenceGeneration() { return reference; }
bool ReadHeadTrackingRoom(float& yaw, Vec3& origin, unsigned long long& gen)
{ yaw = 0; origin = {0, 1.7f, 0}; gen = reference; return room; }
bool AdjustHeadTrackingRoom(float, const Vec3&, unsigned long long) { return true; }
bool QueryBodySegment(std::uintptr_t, Vec3, Vec3, scene::Hit&) { return false; }
bool NewestWristArm(WristArmFrame&) { return false; }
float ViewHookLastNearPlane() { return .1f; }
DWORD SetBodyPose(unsigned) { return 0; }
DWORD SetBodyFeet(unsigned) { return 0; }
std::string BodyPoseReport() { return {}; }
}
int main()
{
    try {
        using namespace preyvr; using namespace preyvr::dll;
        Reset(); Crouch(.45f);
        Check(!gCrouchOwned && !TakePhysicalCrouchTap(sample, true), "no acquisition before stable baseline");
        Baseline(); Check(gHaveStandingEye, "learn standing positive control");
        Crouch(.45f); Check(gCrouchOwned && TakePhysicalCrouchTap(sample, true), "scoped crouch positive control");
        Check(!TakePhysicalCrouchTap(sample, true), "one press per request");
        Stance(kStanceSneak); Eye(1.0f);
        Check(CrouchEyeLift() > .69f, "owned eye lift positive control");
        actor = actorB; Stance(kStanceSneak);
        Check(CrouchEyeLift() == 0, "replacement rejects old lift before next view callback");
        Eye(1.0f); Crouch(0);
        Check(!gCrouchOwned && CrouchEyeLift() == 0 && !TakePhysicalCrouchTap(sample, true), "replacement never inherits crouch/cleanup");
        Stance(kStanceStand); Baseline(1.5f);
        Check(gHaveStandingEye && std::fabs(gStandingEye - 1.5f) < 1e-5f, "replacement learns own baseline");
        Reset(); Baseline(); Crouch(.45f);
        Check(!TakePhysicalCrouchTap(sample, false) && !TakePhysicalCrouchTap(sample, true), "modal refusal discards request");
        AskCrouchTap(); ++sample.epoch;
        Check(!TakePhysicalCrouchTap(sample, true), "new epoch refuses queued input");
        Eye(1.7f); Check(!gCrouchOwned && !gHaveStandingEye, "epoch clears baseline/ownership");
        Baseline(); Crouch(.45f); reference += 2;
        Check(!TakePhysicalCrouchTap(sample, true) && CrouchEyeLift() == 0, "recenter invalidates input and lift immediately");
        Eye(1.7f); Check(!gCrouchOwned, "reference rebind clears ownership");
        Reset(); Baseline(); Crouch(.45f); Stance(kStanceSneak); Eye(1.0f);
        reference += 2; Eye(1.0f);
        Check(gCrouchOwned && CrouchEyeLift() > .69f && !TakePhysicalCrouchTap(sample, true),
              "recenter preserves acknowledged native crouch compensation, not queued input");
        Reset(); Baseline(); Crouch(.45f); Stance(kStanceSneak); Eye(1.0f);
        SetPhysicalCrouch(0); Check(CrouchEyeLift() == 0, "disable clears published lift immediately");
        Crouch(0); Check(!gCrouchOwned && TakePhysicalCrouchTap(sample, true), "disable cleanup only for bound actor");
        Crouch(0); Check(!TakePhysicalCrouchTap(sample, true), "disable cleanup does not repeat");
        Reset(); Baseline(); Crouch(.45f); Stance(kStanceSneak); Eye(1.0f);
        SetBodyFollowActive(false); Check(CrouchEyeLift() == 0 && !TakePhysicalCrouchTap(sample, true), "VR stop invalidates immediately");
        Eye(1.0f); Check(!gCrouchOwned && !gHaveStandingEye, "VR restart cannot retain standing baseline");
        Reset(); Baseline(); Crouch(.45f); tracked = false; Eye(1.0f);
        Check(!gCrouchOwned && CrouchEyeLift() == 0, "tracking loss clears owned state");
        Reset(); Baseline(); Crouch(.45f);
        gCrouchTap.Publish({gFollowContext, MonotonicNanoseconds() - 300000000ull});
        Check(!TakePhysicalCrouchTap(sample, true), "expired press refused");
        std::cout << "body_follow_runtime ok: scoped crouch, baseline, modal, epoch/reference, disable and stop\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
