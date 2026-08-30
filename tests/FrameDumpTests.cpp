#include "preyvr/FrameDump.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using preyvr::framedump::Compare;
using preyvr::framedump::DecodeHeader;
using preyvr::framedump::EncodeHeader;
using preyvr::framedump::Header;
using preyvr::framedump::kHeaderSize;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool Near(double left, double right, double epsilon = 1e-9)
{
    return std::fabs(left - right) <= epsilon;
}

// A 2x2 RGBA image with a caller-chosen row pitch, so the padding case can be
// exercised without a GPU.
std::vector<std::uint8_t> MakeImage(
    std::uint32_t rowPitch,
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b,
    std::uint8_t a)
{
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(rowPitch) * 2u, 0xCD);
    for (std::uint32_t y = 0; y < 2; ++y) {
        for (std::uint32_t x = 0; x < 2; ++x) {
            std::uint8_t* pixel = payload.data() + y * rowPitch + x * 4u;
            pixel[0] = r;
            pixel[1] = g;
            pixel[2] = b;
            pixel[3] = a;
        }
    }
    return payload;
}

Header MakeHeader(std::uint32_t rowPitch)
{
    Header header{};
    header.width = 2;
    header.height = 2;
    header.rowPitch = rowPitch;
    header.frameIndex = 1234;
    header.tag = 7;
    return header;
}

std::vector<std::uint8_t> MakeFile(const Header& header, const std::vector<std::uint8_t>& payload)
{
    std::vector<std::uint8_t> file(kHeaderSize);
    Require(EncodeHeader(header, file), "header encodes into an exact-size buffer");
    file.insert(file.end(), payload.begin(), payload.end());
    return file;
}

void TestHeaderRoundTrip()
{
    const Header header = MakeHeader(8);
    const auto file = MakeFile(header, MakeImage(8, 10, 20, 30, 255));

    const auto decoded = DecodeHeader(file);
    Require(decoded.has_value(), "a well-formed dump decodes");
    Require(decoded->width == 2 && decoded->height == 2, "dimensions survive the round trip");
    Require(decoded->rowPitch == 8, "row pitch survives the round trip");
    Require(decoded->frameIndex == 1234, "frame index survives the round trip");
    Require(decoded->tag == 7, "tag survives the round trip");
    Require(decoded->dxgiFormat == preyvr::framedump::kFormatR8G8B8A8Unorm,
        "format defaults to the captured swapchain format");
}

void TestHeaderFailsClosed()
{
    const Header header = MakeHeader(8);
    const auto payload = MakeImage(8, 10, 20, 30, 255);

    Require(!DecodeHeader(std::span<const std::uint8_t>{}), "an empty buffer fails closed");

    auto shortBuffer = MakeFile(header, payload);
    shortBuffer.resize(kHeaderSize - 1);
    Require(!DecodeHeader(shortBuffer), "a buffer shorter than the header fails closed");

    auto badMagic = MakeFile(header, payload);
    badMagic[0] = 'X';
    Require(!DecodeHeader(badMagic), "a wrong magic fails closed");

    auto badVersion = MakeFile(header, payload);
    badVersion[8] = 99;
    Require(!DecodeHeader(badVersion), "an unknown version fails closed");

    auto zeroWidth = MakeFile(header, payload);
    zeroWidth[12] = 0;
    zeroWidth[13] = 0;
    zeroWidth[14] = 0;
    zeroWidth[15] = 0;
    Require(!DecodeHeader(zeroWidth), "a zero width fails closed");

    // The realistic corruption: a capture interrupted part way through the write.
    auto truncated = MakeFile(header, payload);
    truncated.pop_back();
    Require(!DecodeHeader(truncated), "a truncated payload fails closed");

    auto overlong = MakeFile(header, payload);
    overlong.push_back(0);
    Require(!DecodeHeader(overlong), "a payload longer than the header claims fails closed");

    Header narrowPitch = MakeHeader(4); // width 2 needs 8 bytes per row
    std::vector<std::uint8_t> file(kHeaderSize);
    Require(EncodeHeader(narrowPitch, file), "an inconsistent header still encodes");
    file.resize(kHeaderSize + 8);
    Require(!DecodeHeader(file), "a row pitch too small for the width fails closed");

    Header huge = MakeHeader(8);
    huge.width = preyvr::framedump::kMaxDimension + 1;
    std::vector<std::uint8_t> hugeFile(kHeaderSize);
    Require(EncodeHeader(huge, hugeFile), "an oversized header still encodes");
    Require(!DecodeHeader(hugeFile), "an oversized dimension fails closed");
}

void TestIdenticalFramesCompareToZero()
{
    const Header header = MakeHeader(8);
    const auto a = MakeImage(8, 10, 20, 30, 255);
    const auto b = MakeImage(8, 10, 20, 30, 255);

    const auto difference = Compare(header, a, header, b);
    Require(difference.comparable, "identical dumps are comparable");
    Require(Near(difference.meanAbsolute, 0.0), "identical dumps have zero mean difference");
    Require(difference.maxAbsolute == 0, "identical dumps have zero maximum difference");
    Require(Near(difference.changedPixelRatio, 0.0), "identical dumps have no changed pixels");
    Require(difference.pixelsCompared == 4, "every pixel is compared");
}

void TestKnownDifferenceIsExact()
{
    const Header header = MakeHeader(8);
    const auto a = MakeImage(8, 10, 20, 30, 255);
    auto b = MakeImage(8, 10, 20, 30, 255);
    b[2] = 40; // one pixel's blue channel moves by 10

    const auto difference = Compare(header, a, header, b);
    Require(difference.comparable, "a single-channel change is still comparable");
    Require(difference.maxAbsolute == 10, "the maximum difference is the actual channel delta");
    // One channel of one pixel differs by 10, spread over 4 pixels x 3 channels.
    Require(Near(difference.meanAbsolute, 10.0 / 12.0, 1e-12), "the mean is exact, not approximate");
    Require(Near(difference.changedPixelRatio, 0.25), "one of four pixels counts as changed");
}

void TestAlphaIsIgnored()
{
    const Header header = MakeHeader(8);
    const auto a = MakeImage(8, 10, 20, 30, 255);
    const auto b = MakeImage(8, 10, 20, 30, 0);

    const auto difference = Compare(header, a, header, b);
    Require(difference.comparable, "an alpha-only change is comparable");
    Require(difference.maxAbsolute == 0, "alpha differences do not register");
    Require(Near(difference.meanAbsolute, 0.0), "alpha does not contribute to the mean");
}

void TestDifferentRowPitchStillCompares()
{
    // D3D11 picks the staging pitch and does not promise it is stable between
    // captures. Comparing on a shared stride would quietly produce garbage.
    const Header tight = MakeHeader(8);
    const Header padded = MakeHeader(24);
    const auto a = MakeImage(8, 10, 20, 30, 255);
    const auto b = MakeImage(24, 10, 20, 30, 255);

    const auto difference = Compare(tight, a, padded, b);
    Require(difference.comparable, "dumps with different row pitches are comparable");
    Require(difference.maxAbsolute == 0, "row padding does not leak into the comparison");
    Require(difference.pixelsCompared == 4, "padding is not counted as pixels");
}

void TestMismatchedFramesAreNotComparable()
{
    Header wide = MakeHeader(8);
    wide.width = 4;
    wide.rowPitch = 16;

    const Header header = MakeHeader(8);
    const auto a = MakeImage(8, 10, 20, 30, 255);
    const std::vector<std::uint8_t> b(static_cast<std::size_t>(16) * 2u, 0);

    const auto difference = Compare(header, a, wide, b);
    Require(!difference.comparable, "different dimensions are not comparable");
    Require(difference.pixelsCompared == 0,
        "an incomparable result reports no pixels, so it cannot be read as a zero difference");
}

void TestShortPayloadIsRejected()
{
    const Header header = MakeHeader(8);
    const auto a = MakeImage(8, 10, 20, 30, 255);
    const std::vector<std::uint8_t> tooShort(8, 0);

    const auto difference = Compare(header, a, header, tooShort);
    Require(!difference.comparable, "a payload shorter than its header claims is not comparable");
}

} // namespace

int main()
{
    TestHeaderRoundTrip();
    TestHeaderFailsClosed();
    TestIdenticalFramesCompareToZero();
    TestKnownDifferenceIsExact();
    TestAlphaIsIgnored();
    TestDifferentRowPitchStillCompares();
    TestMismatchedFramesAreNotComparable();
    TestShortPayloadIsRejected();
    std::cout << "PreyVR frame dump tests passed\n";
    return 0;
}
