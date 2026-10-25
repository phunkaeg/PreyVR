#pragma once

#include "preyvr/UiPanel.h"
#include <cstdint>
#include <optional>

namespace preyvr::ui {

struct PokePoint {
    float u = 0, v = 0;
    // Metres toward the viewer/front face. Positive is in front of the panel;
    // negative is through it. The visible cylinder faces toward its axis.
    float frontDistance = 0;
    Vec3 point{}, normal{};
    bool inside = false;
};

// Nearest point on the extended flat/cylindrical surface. UV is intentionally
// unclipped, allowing callers to cancel an outside drag without edge clamping.
std::optional<PokePoint> ProjectPoke(const Surface& surface, Vec3 fingertip);

struct PokeSettings {
    float frontHover = .08f;
    float armDistance = .015f;
    float releaseDistance = .015f;
    float pressDistance = -.002f;
    float maxPenetration = .06f;
    float maxFingerJump = .15f;
    float maxSurfaceMove = .01f;
    float maxSurfaceTurn = .035f; // radians; quaternion sign does not matter
    std::uint64_t maxAgeNs = 200000000ull;
    std::uint64_t maxGapNs = 200000000ull;
};

struct PokeInput {
    bool valid = false;
    Surface surface{};
    Vec3 tip{};
    std::uint64_t nowNs = 0, stampNs = 0, sequence = 0;
    // epoch/reference identify tracking coordinates; generation identifies the
    // owning hand, panel/content, input mode and settings. Bump it whenever any
    // of those ownership meanings changes, even if the surface pose is identical.
    std::uint64_t epoch = 0, reference = 0, generation = 0;
};

struct PokeOutput {
    // active is near-touch ownership: inside the panel and within the bounded
    // hover/penetration interval. Callers can suppress the ray while active.
    bool active = false, inside = false, pressed = false;
    std::optional<PokePoint> contact;
};

// A press needs a fresh front hover followed by physical fingertip crossing.
// Returns button state, not edges: feed active/inside/pressed to PointerButtons.
// Reset/cancellation returns up; the finger must withdraw before another press.
// An identical duplicate publication retains its previous state until stale.
class Poke {
public:
    PokeOutput Update(const PokeInput& input, const PokeSettings& settings = {});
    void Reset();
private:
    PokeInput previous_{};
    PokeOutput output_{};
    bool havePrevious_ = false, armed_ = false, down_ = false;
};

} // namespace preyvr::ui
