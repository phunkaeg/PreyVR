#pragma once

#include "preyvr/VrMath.h"

#include <cmath>
#include <cstdint>
#include <optional>

// World interaction, phase 1: the LEFT hand points at what to use and its grip
// uses it; the right grip only reloads. Everything after the press is the
// game's own (pick up, carry, open, loot): the mod only chooses the ray the
// native selector looks along and when the native use button is held.
//
// **Why the left hand.** It is the free hand: the right one holds the weapon and
// keeps aiming while the left one opens a locker or picks up a can. It is how
// Half-Life: Alyx and the other good VR games split the hands, and it is the
// hand Funk's grab research (PHYSICAL-GRAB-SEAMS-2026-10-04) planned for.
//
// **Why the right grip has to stop using.** Prey has ONE gamepad button for use
// and reload (X): with something selected it uses it, with nothing it reloads.
// Once the selector looks along the left hand, an X from the right grip would
// use whatever the LEFT hand points at. So the right grip asks the selector to
// find nothing for a couple of frames first, and only then holds X: a reload,
// whatever the left hand is pointing at.
//
// Pure decisions, testable offline; the DLL (InteractionLane.cpp, MoveLane.cpp)
// supplies the frame's inputs and carries out the outputs.
namespace preyvr::use {

// --- the use button ---------------------------------------------------------
struct ButtonInput {
    bool gameplay = false;   // gameplay input allowed and the action lane on
    bool leftHand = true;    // use.hand: true = left grip uses, right grip reloads
    bool leftGrip = false, rightGrip = false;
    // Another lane owns that grip this frame, or would take it on this press:
    // left = medkit slot, two-hand foregrip, recentre chord; right = holster,
    // two-hand hold, recentre chord, paired squeeze.
    bool leftBusy = false, rightBusy = false;
    // The game has a usable entity right now (the selector's committed pick).
    bool target = false;
};

enum class Owner : std::uint8_t { None, Use, Reload, Legacy };
const char* OwnerName(Owner owner);

struct ButtonOutput {
    bool down = false;            // the native use/reload button is held this frame
    bool suppressTarget = false;  // the selector must find nothing this frame
    Owner owner = Owner::None;
    // One-frame events, for counters and the trace.
    bool pressed = false, released = false;
    bool noTarget = false;        // left grip pressed with nothing selected: nothing sent
    bool gaveUp = false;          // reload: the selection never cleared, nothing sent
    bool busy = false;            // a press another lane owned
};

class UseButton {
public:
    // Frames the selector must report nothing before X goes down for a reload,
    // and how long it is waited for. The selector runs once per frame, so two
    // frames is one frame of margin; twelve is ~130 ms at 90 Hz.
    static constexpr int kClearFrames = 2;
    static constexpr int kGiveUpFrames = 12;
    // After a reload's X is released the selector stays empty this long, so the
    // release cannot land on a target (a native "tap" can act on release).
    static constexpr int kGuardFrames = 3;

    ButtonOutput Update(const ButtonInput& in);
    void Reset();
    Owner CurrentOwner() const { return owner_; }

private:
    Owner owner_ = Owner::None;
    bool down_ = false;
    bool leftArmed_ = false, rightArmed_ = false;   // released since the last press
    int clear_ = 0, waited_ = 0, guard_ = 0;
};

// --- the pointing ray -------------------------------------------------------
//
// One Euro filter (Casiez et al. 2012): a low-pass whose cutoff rises with
// speed, so a still hand does not tremble and a moving one does not lag. The
// Dishonored VR pointer measured 0 flips between two adjacent objects with
// these numbers and a 0.4-degree tremor, where the raw ray flipped every frame.
struct FilterSettings {
    bool enabled = true;
    float minCutoff = 1.0f;       // Hz, direction at rest
    float beta = 2.0f;            // direction cutoff gain per unit/s of change
    float positionMinCutoff = 2.0f;
    float positionBeta = 6.0f;    // per m/s
    float derivativeCutoff = 1.0f;
    // Backlash after the One Euro stage: the output does not move until the
    // hand has turned further than this from it, then follows at that
    // distance. Measured 2026-10-06 in the mock with a 0.4-degree tremor
    // aimed exactly at the edge between two props: the One Euro stage alone
    // left 22-25 selection flips in 8 s (raw: 32-43); a dead band wider than
    // the tremor cannot flip. Costs at most this much aiming error, far inside
    // the selector's own 11.5/16.3-degree cones.
    float backlashDegrees = .75f;
    float backlashMetres = .004f;
    // A jump this large (a recentre, a teleport, tracking returning) restarts
    // the filter instead of sliding across it.
    float resetDegrees = 15.0f;
    float resetMetres = .3f;
};

class OneEuro {
public:
    float Update(float value, float dt, float minCutoff, float beta, float derivativeCutoff);
    void Reset() { primed_ = false; }

private:
    bool primed_ = false;
    float value_ = 0, derivative_ = 0;
};

struct Ray {
    Vec3 origin{}, direction{};
};

class RayFilter {
public:
    // `direction` must be unit length; the result's is. dt in seconds.
    Ray Update(const Ray& raw, float dt, const FilterSettings& settings);
    void Reset();
    bool Primed() const { return primed_; }

private:
    bool primed_ = false;
    Ray last_{}, held_{};
    OneEuro origin_[3], direction_[3];
};

// --- geometry ---------------------------------------------------------------
float AngleDegrees(Vec3 a, Vec3 b);
// Where a ray enters an axis-aligned box (t >= 0), if it does.
std::optional<float> RayBoxEntry(Vec3 origin, Vec3 direction, Vec3 boxMin, Vec3 boxMax);
// Where the drawn pointer should end on a target: the entry point when the ray
// passes through the target's bounds, otherwise the bounds point nearest the
// ray (the native selector accepts targets inside its cone, not only on the
// line, and the pointer bends to show which one it took).
Vec3 PointerEnd(Vec3 origin, Vec3 direction, Vec3 boxMin, Vec3 boxMax, bool* onLine = nullptr);

}  // namespace preyvr::use
