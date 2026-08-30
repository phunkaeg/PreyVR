#include "preyvr/XrSwapchainFormat.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace preyvr::xrswapchain;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void TestExactMatchWins()
{
    // Prey's backbuffer format, captured live 2026-08-29.
    const std::vector<std::int64_t> offered{
        kR8G8B8A8UnormSrgb, kB8G8R8A8UnormSrgb, kR8G8B8A8Unorm, kB8G8R8A8Unorm};

    const auto choice = SelectFormat(offered);
    Require(choice.has_value(), "a format is chosen");
    Require(choice->format == kR8G8B8A8Unorm, "the exact source format is chosen");
    Require(choice->match == Match::exact, "the match is reported as exact");
    Require(!InvolvesColourConversion(*choice), "an exact match involves no conversion");

    // Note the runtime listed both sRGB formats *first*. The specification says
    // that list is in the runtime's preference order, and this policy overrides
    // it on purpose: the runtime's preference is about its own pipeline, and we
    // are copying an already-composited image where a conversion is a hazard
    // rather than a feature.
}

void TestChannelSwapIsPreferredOverSrgb()
{
    const std::vector<std::int64_t> offered{kR8G8B8A8UnormSrgb, kB8G8R8A8Unorm};
    const auto choice = SelectFormat(offered);
    Require(choice.has_value(), "a format is chosen");
    Require(choice->format == kB8G8R8A8Unorm,
        "a channel swap is preferred over a transfer function");
    Require(choice->match == Match::channelSwapped, "the match is reported as a swap");
    Require(!InvolvesColourConversion(*choice), "a channel swap is colour-exact");
}

void TestSrgbIsTheLastResort()
{
    const std::vector<std::int64_t> offered{kR8G8B8A8UnormSrgb};
    const auto choice = SelectFormat(offered);
    Require(choice.has_value(), "an sRGB-only runtime still yields a choice");
    Require(choice->format == kR8G8B8A8UnormSrgb, "the sRGB variant is used");
    Require(choice->match == Match::srgbVariant, "the match is reported as the sRGB variant");
    Require(InvolvesColourConversion(*choice),
        "the caller is told a transfer function is involved, so a flat image has an explanation");

    // The swapped sRGB spelling is also accepted, since a runtime may offer only
    // that one.
    const std::vector<std::int64_t> swappedOnly{kB8G8R8A8UnormSrgb};
    const auto swapped = SelectFormat(swappedOnly);
    Require(swapped.has_value() && swapped->format == kB8G8R8A8UnormSrgb,
        "the swapped sRGB spelling is recognised too");
}

void TestUnrecognisedFormatsFailClosed()
{
    // 10-bit, float, and typeless formats. Copying an 8-bit LDR image into one of
    // these produces something wrong rather than something that fails, so taking
    // the runtime's first entry would be worse than refusing.
    const std::vector<std::int64_t> exotic{2 /*R32G32B32A32_FLOAT*/, 24 /*R10G10B10A2_UNORM*/, 27};
    Require(!SelectFormat(exotic), "an unrecognised format list is refused rather than guessed at");

    Require(!SelectFormat(std::span<const std::int64_t>{}), "an empty list is refused");

    const std::vector<std::int64_t> fine{kR8G8B8A8Unorm};
    Require(!SelectFormat(fine, 0), "a zero source format is refused");
}

void TestOtherSourceFormats()
{
    // If Prey's backbuffer ever changes, the policy should still work from the
    // other side rather than being hardcoded to one direction.
    const std::vector<std::int64_t> offered{kR8G8B8A8Unorm};
    const auto choice = SelectFormat(offered, kB8G8R8A8Unorm);
    Require(choice.has_value() && choice->match == Match::channelSwapped,
        "a BGRA source finds the RGBA swap");
}

} // namespace

int main()
{
    TestExactMatchWins();
    TestChannelSwapIsPreferredOverSrgb();
    TestSrgbIsTheLastResort();
    TestUnrecognisedFormatsFailClosed();
    TestOtherSourceFormats();
    std::cout << "PreyVR XR swapchain format tests passed\n";
    return 0;
}
