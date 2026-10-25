#pragma once

#include <cstdint>

namespace preyvr::hacking {

inline constexpr std::uint8_t AxisX = 1, AxisY = 2;
inline constexpr std::uint8_t ButtonA = 1, ButtonB = 2, ButtonX = 4, ButtonY = 8;

struct Settings {
    float deadzone = .15f, changeEpsilon = .002f;
    std::uint64_t maxAgeNs = 200000000ull, maxGapNs = 200000000ull;
};

struct Input {
    // active must mean the exact native hacking modal, not any visible menu.
    bool valid = false, active = false;
    float stickX = 0, stickY = 0;
    bool a = false, b = false, x = false, y = false;
    std::uint64_t nowNs = 0, stampNs = 0, sequence = 0;
    // generation identifies the player, movie/content and input/settings owner.
    std::uint64_t epoch = 0, reference = 0, generation = 0;
};

struct Output {
    float x = 0, y = 0;
    std::uint8_t axisMask = 0, pressMask = 0, releaseMask = 0;
    std::uint64_t proposal = 0;
    bool ready = false;
};

// Single-thread owned; no native calls, dynamic storage or clock access.
// Update proposes native pad state but does not assume delivery. After posting,
// copy that Output, remove refused channels from its masks, then Commit it.
// Commit uses only proposed values and accepts partial success. Unacknowledged
// channels retry on the next Update, even for the same source publication.
// Accepted button edges never repeat. Out-of-order acknowledgements are ignored.
//
// A context change/suspend first releases old owned controls. Neutral input and
// acknowledged zero axes are required before a new context can acquire input.
// Start/options remain outside this router.
class InputState {
public:
    Output Update(const Input& input, const Settings& settings = {});
    Output Suspend();
    void Commit(const Output& accepted);
    // Interlock for other native input lanes: pending initial zero writes do
    // not own controls, but any delivered nonzero axis/held button does.
    bool OwnsControls() const;
private:
    Output Propose(float x, float y, std::uint8_t buttons);
    bool CleanupPending() const;
    Input previous_{};
    Output pending_{};
    std::uint64_t serial_ = 0;
    float sentX_ = 0, sentY_ = 0, epsilon_ = .002f;
    std::uint8_t sentButtons_ = 0, primeMask_ = 0;
    bool havePrevious_ = false, ready_ = false;
};

} // namespace preyvr::hacking
