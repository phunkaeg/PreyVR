#pragma once

#include <cstdint>
#include <optional>
#include <span>

// Choosing the OpenXR swapchain format to copy Prey's backbuffer into.
//
// This is a small decision with a large and confusing failure mode. The runtime
// offers a list of formats it supports; the app picks one. Pick a format whose
// colour-space interpretation disagrees with the source and nothing errors --
// the image is simply washed out or too dark, and it looks like a brightness
// setting rather than a bug. That is precisely the class of failure this project
// keeps trying to catch before a headset is involved.
//
// The measured constraint: Prey's swapchain is `DXGI_FORMAT_R8G8B8A8_UNORM`
// (28), **not** the sRGB variant, with `SampleDesc.Count = 1`. Captured live on
// 2026-08-29. What the backbuffer holds at that point is a finished, already
// gamma-encoded image, because it is what would have been presented.
//
// **What this header does not do is assert which choice is correct.** Whether an
// sRGB swapchain double-corrects or round-trips depends on what the runtime does
// on its side, and that is a property of VirtualDesktopXR rather than something
// derivable here. So the policy prefers the format that involves no conversion at
// all, records the alternative, and leaves the question to be *measured* on the
// first headset run -- the same discipline as the frame-difference threshold.
namespace preyvr::xrswapchain {

// DXGI values, as OpenXR reports them for the D3D11 extension.
inline constexpr std::int64_t kR8G8B8A8Unorm = 28;
inline constexpr std::int64_t kR8G8B8A8UnormSrgb = 29;
inline constexpr std::int64_t kB8G8R8A8Unorm = 87;
inline constexpr std::int64_t kB8G8R8A8UnormSrgb = 91;

enum class Match {
    exact = 0,        // identical to the source: no conversion anywhere
    channelSwapped = 1, // same encoding, BGRA vs RGBA
    srgbVariant = 2,  // same channels, but the runtime will apply a transfer function
    none = 3,
};

struct Choice {
    std::int64_t format = 0;
    Match match = Match::none;
};

// `runtimeFormats` is what xrEnumerateSwapchainFormats returned, **in the
// runtime's own preference order**, which the specification says is most- to
// least-preferred. `sourceFormat` is the game's backbuffer format.
//
// Preference order, and the reasoning:
//
//   1. the exact source format -- no conversion, nothing to get wrong
//   2. the channel-swapped equivalent -- a swizzle is cheap and colour-exact
//   3. the sRGB variant -- works, but introduces a transfer function whose
//      correctness depends on the runtime, so it is chosen only if nothing
//      better is offered and the caller should expect to verify brightness
//
// Returns nothing if the runtime offers none of these, which is a real
// possibility worth failing closed on rather than picking the first entry and
// hoping: an unrecognised format could be 10-bit, float, or typeless, and
// copying an 8-bit LDR image into it silently produces something wrong.
std::optional<Choice> SelectFormat(
    std::span<const std::int64_t> runtimeFormats,
    std::int64_t sourceFormat = kR8G8B8A8Unorm);

// True when the choice will make the runtime apply a transfer function that the
// source has already had applied. Exposed so the caller can say so in the log
// rather than leaving a future reader to wonder why the image looks flat.
bool InvolvesColourConversion(const Choice& choice);

} // namespace preyvr::xrswapchain
