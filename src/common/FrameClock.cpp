#include "preyvr/FrameClock.h"

namespace preyvr::frameclock {

EyeDecision DecideEye(const Timeline& previous, const Timeline& current,
                      std::uint64_t tolerance)
{
    EyeDecision out{};

    // A clock that goes backwards is not drift, it is a broken timeline. Refuse
    // rather than compute a plausible eye from nonsense.
    if (current.engine < previous.engine || current.render < previous.render ||
        current.present < previous.present) {
        out.invalid = true;
        return out;
    }

    // Signed, and computed in 64-bit so a large present cannot wrap the
    // subtraction into a small positive drift.
    out.drift = static_cast<std::int64_t>(current.render) -
                static_cast<std::int64_t>(current.present);

    const std::int64_t limit = static_cast<std::int64_t>(tolerance);
    if (out.drift < 0 || out.drift > limit) {
        // Render behind present is impossible in a sane pipeline, and render too
        // far ahead means the parity no longer names the eye that is about to be
        // shown. Both refuse.
        out.drifted = true;
        return out;
    }

    // **The presenter's parity, not a counter of our own.** This is the whole
    // point: the eye is a property of the engine's frame phase, so it stays
    // correct across a skipped or repeated frame that a private counter would
    // silently mislabel.
    out.eye = static_cast<int>(current.present & 1ull);
    return out;
}

} // namespace preyvr::frameclock
