#include "preyvr/XrSwapchainFormat.h"

#include <algorithm>

namespace preyvr::xrswapchain {
namespace {

std::int64_t SwappedChannels(std::int64_t format)
{
    switch (format) {
        case kR8G8B8A8Unorm:     return kB8G8R8A8Unorm;
        case kB8G8R8A8Unorm:     return kR8G8B8A8Unorm;
        case kR8G8B8A8UnormSrgb: return kB8G8R8A8UnormSrgb;
        case kB8G8R8A8UnormSrgb: return kR8G8B8A8UnormSrgb;
        default:                 return 0;
    }
}

std::int64_t SrgbVariant(std::int64_t format)
{
    switch (format) {
        case kR8G8B8A8Unorm: return kR8G8B8A8UnormSrgb;
        case kB8G8R8A8Unorm: return kB8G8R8A8UnormSrgb;
        default:             return 0;
    }
}

bool Offered(std::span<const std::int64_t> formats, std::int64_t wanted)
{
    return wanted != 0 &&
           std::find(formats.begin(), formats.end(), wanted) != formats.end();
}

} // namespace

std::optional<Choice> SelectFormat(
    std::span<const std::int64_t> runtimeFormats,
    std::int64_t sourceFormat,
    bool preferSrgb)
{
    if (runtimeFormats.empty() || sourceFormat == 0) {
        return std::nullopt;
    }

    // **Why an exact match can be the wrong answer -- FAIL-STR-033.**
    //
    // Prey presents already sRGB-encoded bytes in a plain `_UNORM` surface,
    // because that is simply what it would have sent to a monitor. Taking the
    // exact match hands those bytes to a swapchain the compositor treats as
    // linear, so they are encoded a second time and the image goes milky.
    //
    // Measured on this target 2026-09-02: format 28 (`R8G8B8A8_UNORM`),
    // `colour_conversion=no`, and washed out through the headset on
    // VirtualDesktopXR -- in the flat mirror as well as in stereo, which is what
    // ruled out the stereo copy path as the cause.
    //
    // Naming the `_SRGB` variant instead makes the hardware decode once on read,
    // which is the free and correct fix. It is a **preference rather than the
    // default** because the fleet playbook is explicit that this can be a
    // property of the runtime rather than of the format, and must be measured
    // per runtime instead of assumed once.
    if (preferSrgb) {
        const std::int64_t srgb = SrgbVariant(sourceFormat);
        if (Offered(runtimeFormats, srgb)) {
            return Choice{srgb, Match::srgbVariant};
        }
        const std::int64_t swappedSrgbFirst = SwappedChannels(srgb);
        if (Offered(runtimeFormats, swappedSrgbFirst)) {
            return Choice{swappedSrgbFirst, Match::srgbVariant};
        }
        // Nothing sRGB on offer. Fall through rather than fail: an exact match
        // that looks wrong beats no session at all, and the log records which
        // was taken.
    }

    if (Offered(runtimeFormats, sourceFormat)) {
        return Choice{sourceFormat, Match::exact};
    }

    const std::int64_t swapped = SwappedChannels(sourceFormat);
    if (Offered(runtimeFormats, swapped)) {
        return Choice{swapped, Match::channelSwapped};
    }

    // Both sRGB spellings, since the runtime may offer only the swapped one.
    const std::int64_t srgb = SrgbVariant(sourceFormat);
    if (Offered(runtimeFormats, srgb)) {
        return Choice{srgb, Match::srgbVariant};
    }
    const std::int64_t swappedSrgb = SwappedChannels(srgb);
    if (Offered(runtimeFormats, swappedSrgb)) {
        return Choice{swappedSrgb, Match::srgbVariant};
    }

    // Deliberately not falling back to runtimeFormats[0]. An unrecognised format
    // could be 10-bit, float, or typeless, and copying an 8-bit LDR image into
    // one of those produces something wrong rather than something that fails.
    return std::nullopt;
}

bool InvolvesColourConversion(const Choice& choice)
{
    return choice.match == Match::srgbVariant;
}

} // namespace preyvr::xrswapchain
