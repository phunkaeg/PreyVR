#pragma once
#include "preyvr/VrMath.h"

namespace preyvr::twohand {
// Engine axes, metres. Endpoints are relative to the primary wrist in the
// authored barrel frame. The animation observer supplies these, not XR poses.
struct Region { Vec3 start{}, end{}; float radius = .10f; };
struct Input {
    Pose aim{}, support{};
    Vec3 primary{}, visualPrimaryOffset{};
    Region region{};
    float squeeze = 0, dt = 0;
    std::uint64_t owner = 0, reference = 0, epoch = 0;
    bool usable = false;
    bool toggleGrip = false;
};
struct Output {
    Quaternion orientation{}, correction{}, supportOrientation{};
    Vec3 socket{};
    float blend = 0;
    bool held = false;
    bool snapSupport = true;
};
class Solver {
public:
    Output Update(const Input& input);
private:
    std::uint64_t owner_ = 0, reference_ = 0, epoch_ = 0;
    bool held_ = false, armed_ = false;
    bool toggleGrip_ = false;
    float blend_ = 0;
    Vec3 socket_{};
    Quaternion swing_{}, supportInAim_{};
};
// The support socket of one weapon, from the native offhand. The foregrip is a
// rigid part of the model but the native left palm is animated: firing, cooling
// and reload animations take it off the handle (Q-Beam, measured 2026-10-07:
// 375,616,198 mm at rest; 580,763,168 / 129,473,96 / 280,647,433 mm a few
// seconds into a sustained beam). Read raw, the region wandered with the
// animation and, the first frame it left the acceptance bounds, was withdrawn,
// which released a held foregrip mid-fight. The socket is taken once the palm
// has rested within kSettleMetres for kSettleSamples consecutive in-bounds
// samples, and kept for that weapon: an animated or out-of-bounds palm neither
// moves nor withdraws it. A new owner (re-equip) starts over.
class RegionLatch {
public:
    static constexpr int kSettleSamples = 20;
    static constexpr float kSettleMetres = .01f;
    // `candidateValid`: the palm was read and lies inside the acceptance bounds.
    // Returns true with the weapon's socket once one is latched for `owner`.
    bool Update(std::uint64_t owner, bool candidateValid, Vec3 candidate, Vec3& socket);
    bool Latched() const { return latched_; }
    Vec3 Socket() const { return socket_; }
private:
    std::uint64_t owner_ = 0;
    bool latched_ = false;
    int steady_ = 0;
    Vec3 anchor_{}, socket_{};
};
// **Long weapons are held further back in VR.** A flat-screen viewmodel is
// posed for a camera, not for an arm: the Q-Beam's native foregrip is 0.58 m
// ahead of the trigger wrist, 0.36 m beside it and 0.23 m below (2026-10-07),
// 0.73-0.92 m from a player's left shoulder when the right hand is anywhere
// natural -- beyond any arm (~0.60 m to the palm). VR games put a real-size
// foregrip within reach and snap the support hand onto it from a grab radius;
// here the weapon (with the drawn trigger hand on its own grip) moves back along
// the barrel by however much its foregrip lies beyond `comfortForward`, at most
// `maxHoldBack`, and the grab region reaches `supportBack` behind the foregrip.
// A weapon whose foregrip is within reach (GLOO) is not touched. Metres.
struct LongReach {
    float comfortForward = .45f;
    float maxHoldBack = .20f;
    float supportBack = .10f;
};
// `socket` is the foregrip in the barrel frame from the trigger wrist (+Y ahead).
float HoldBack(Vec3 socket, const LongReach& reach);
// The grab region along the barrel: from `back` behind the foregrip to 4 cm ahead.
Region SupportRegion(Vec3 socket, float holdBack, const LongReach& reach);
// Also useful for diagnostics/authoring. Refuses malformed regions and poses.
bool ClosestGrip(const Input& input, Vec3& localPoint);
} // namespace preyvr::twohand
