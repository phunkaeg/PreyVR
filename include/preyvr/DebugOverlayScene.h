#pragma once

#include "preyvr/DebugDraw.h"

#include <array>
#include <cstdint>
#include <string>

// What the in-game debug overlay shows, decided from plain state.
//
// The DLL gathers snapshots from the systems that own them (aim, holsters,
// medkit slot, wrist, two-hand solver) and converts every position into the
// app's OpenXR space. This file only decides how that state looks, so the
// choice of colours and what counts as "inside a slot" is testable offline and
// cannot drift from the numbers the systems themselves use.
namespace preyvr::debugdraw {

// `dbg.draw` mask bits.
enum Layer : unsigned {
    kRays = 1u << 0,      // each hand's ray as the game uses it, with its target
    kRaw = 1u << 1,       // controller grip axes and the raw aim ray, dashed
    kBody = 1u << 2,      // holster and medkit slots, hand tethers
    kWrist = 1u << 3,     // wrist card frame and its visibility test
    kForegrip = 1u << 4,  // two-hand support region and socket
    kLabels = 1u << 5,    // text beside each primitive
    kStatus = 1u << 6,    // the status panel below the view
    kHits = 1u << 7,      // scene queries along each ray (otherwise rays end at a fixed range)
};
inline constexpr unsigned kAllLayers = 0xFFu;

// Colours, shared with documentation: amber = right, cyan = left, violet = psi.
inline constexpr Color kRight{255, 176, 64, 235};
inline constexpr Color kLeft{80, 205, 255, 235};
inline constexpr Color kPsi{200, 120, 255, 240};
inline constexpr Color kIdle{235, 235, 235, 170};
inline constexpr Color kNear{255, 220, 70, 230};
inline constexpr Color kActive{90, 235, 120, 245};
inline constexpr Color kWarn{255, 150, 40, 245};
inline constexpr Color kDenied{255, 70, 70, 245};
inline constexpr Color kStored{110, 160, 255, 235};
inline constexpr Color kMuted{150, 150, 150, 140};

enum class RayRole : std::uint8_t { None, Weapon, Psi, Pointer, Use };
// The use pointer's states (InteractionLane): pointing at something the game
// selected, using it (button held), or blanked while the right grip reloads.
inline constexpr Color kUseHeld{90, 235, 120, 245};
inline constexpr Color kUseIdle{80, 205, 255, 120};
// One hand's ray. `origin`/`direction` are what is drawn from (the controller's
// aim pose); `target` is where the game's own ray ends -- its scene hit when
// `hit`, otherwise the fallback range. For the weapon the game's ray starts at
// the eye, so the drawn origin-to-target line is the converging shot path, and
// the raw aim ray beside it shows the parallax at close range.
struct HandRay {
    bool valid = false;
    RayRole role = RayRole::None;
    Vec3 origin{}, direction{}, target{};
    bool hit = false;
    float distance = 0;
    // aim.shot: the ray starts at the weapon's muzzle and IS the shot's path.
    bool fromMuzzle = false;
    // RayRole::Use: what the game selected along this ray.
    bool selected = false;      // `target` is on the selected object's bounds
    bool onLine = false;        // the ray passes through those bounds (else a cone pick)
    bool held = false;          // the use button is down
    bool suppressed = false;    // the right grip is reloading: nothing selectable
    std::string label;          // object name and the game's prompt
    std::array<Vec3, 8> candidates{};
    unsigned candidateCount = 0;
};
// The last shot as the engine fired it: from where the projectile left toward
// the target it was sent to. Drawn for a few seconds after the shot.
inline constexpr float kShotSeconds = 4.0f;
inline constexpr Color kShot{90, 255, 140, 240};
struct ShotMark {
    bool valid = false;
    Vec3 spawn{}, target{};
    bool hit = false;
    bool projectileSeen = false;
    float age = 0;              // seconds since the shot
    float angleDegrees = 0;     // projectile direction against the aim direction
    float spawnOffAimMm = 0;    // spawn's distance from the controller's aim line
    std::string route, spawnKind;
};
struct Controller {
    bool valid = false;
    Pose grip{}, aim{};
    bool gripPressed = false, triggerPressed = false;
};

enum class Notice : std::uint8_t { None, Ready, Success, Empty, Denied };
// One body slot as the gesture that owns it measures it.
struct Slot {
    bool valid = false;        // the gesture is live and has a torso estimate
    Vec3 centre{};
    float radius = .14f;
    Vec3 hand{};               // the controller grip that slot measures
    bool gripPressed = false;
    bool armed = false;        // grip released since the last action: a squeeze will act
    bool owned = false;        // this slot currently owns the grip
    bool stored = false;       // a holster has a weapon assigned
    bool triggerArmed = false; // medkit: trigger released since selection
    Notice notice = Notice::None;
    float noticeAge = 0;       // seconds
    std::string name;          // "HIP R", "CHEST L", "MEDKIT"
};
struct Wrist {
    bool enabled = false;
    bool gate = false;         // every precondition except where the wrist is held
    bool visible = false;
    Pose card{};
    float width = .18f, height = .07f;
    float distance = 0, facing = 0, viewing = 0;
    float facingNeeded = .55f, viewingNeeded = .65f;
};
struct Foregrip {
    bool valid = false;        // the weapon publishes a support region
    Vec3 start{}, end{};
    float radius = .1f;
    bool inRegion = false, held = false;
    float blend = 0;
    Vec3 socket{};
};
struct OverlayFrame {
    bool headValid = false;
    Pose head{};
    std::array<Controller, 2> hands{};   // 0 = left, 1 = right
    std::array<HandRay, 2> rays{};
    ShotMark shot{};
    bool holstersEnabled = false, medkitEnabled = false;
    std::array<Slot, 2> holsters{};      // 0 = right hip, 1 = left chest
    Slot medkit{};
    Wrist wrist{};
    Foregrip foregrip{};
    unsigned psiMode = 0;                // 0 native, 1 head, 2 left controller
    bool sceneQueryFault = false;
    std::string extra;                   // free text appended to the status panel
};

// Builds the scene for one frame. Pure: the same frame always draws the same.
void BuildOverlayScene(const OverlayFrame& frame, unsigned mask, Scene& scene);
// The status panel's text, also used by `dbg.report`.
std::string OverlayStatusText(const OverlayFrame& frame, unsigned mask);
// Which colour a slot is drawn in, the core of the slot legend.
Color SlotColor(const Slot& slot);
// Distance from a slot's hand to its centre, metres.
float SlotHandDistance(const Slot& slot);

}  // namespace preyvr::debugdraw
