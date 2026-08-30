#include "preyvr/FrameDump.h"

#include <algorithm>
#include <cstring>

namespace preyvr::framedump {
namespace {

void WriteU32(std::uint8_t* out, std::uint32_t value) {
    out[0] = static_cast<std::uint8_t>(value & 0xFFu);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    out[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    out[3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

std::uint32_t ReadU32(const std::uint8_t* in) {
    return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8) |
           (static_cast<std::uint32_t>(in[2]) << 16) | (static_cast<std::uint32_t>(in[3]) << 24);
}

void WriteU64(std::uint8_t* out, std::uint64_t value) {
    WriteU32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFull));
    WriteU32(out + 4, static_cast<std::uint32_t>(value >> 32));
}

std::uint64_t ReadU64(const std::uint8_t* in) {
    return static_cast<std::uint64_t>(ReadU32(in)) |
           (static_cast<std::uint64_t>(ReadU32(in + 4)) << 32);
}

// Explicit little-endian encoding rather than a memcpy of the struct: the file
// is read by a PowerShell converter and potentially by other tools, so the
// layout must be a stated contract, not whatever the compiler chose to pad.
constexpr std::size_t kOffMagic = 0;
constexpr std::size_t kOffVersion = 8;
constexpr std::size_t kOffWidth = 12;
constexpr std::size_t kOffHeight = 16;
constexpr std::size_t kOffFormat = 20;
constexpr std::size_t kOffRowPitch = 24;
constexpr std::size_t kOffTag = 28;
constexpr std::size_t kOffFrameIndex = 32;
constexpr std::size_t kOffReserved = 40;

bool HeaderIsSelfConsistent(const Header& header) {
    if (header.version != kVersion) {
        return false;
    }
    if (header.width == 0 || header.height == 0) {
        return false;
    }
    if (header.width > kMaxDimension || header.height > kMaxDimension) {
        return false;
    }
    // Four bytes per pixel is the only format this version describes. Checking
    // the pitch against it catches a header that claims a format we cannot
    // actually walk.
    const std::uint64_t minimumPitch = static_cast<std::uint64_t>(header.width) * 4ull;
    if (header.rowPitch < minimumPitch) {
        return false;
    }
    const std::uint64_t payload =
        static_cast<std::uint64_t>(header.rowPitch) * static_cast<std::uint64_t>(header.height);
    if (payload == 0 || payload > kMaxPayloadBytes) {
        return false;
    }
    return true;
}

} // namespace

bool EncodeHeader(const Header& header, std::span<std::uint8_t> out) {
    if (out.size() < kHeaderSize) {
        return false;
    }
    std::fill(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(kHeaderSize), std::uint8_t{0});
    std::memcpy(out.data() + kOffMagic, kMagic, sizeof(kMagic));
    WriteU32(out.data() + kOffVersion, header.version);
    WriteU32(out.data() + kOffWidth, header.width);
    WriteU32(out.data() + kOffHeight, header.height);
    WriteU32(out.data() + kOffFormat, header.dxgiFormat);
    WriteU32(out.data() + kOffRowPitch, header.rowPitch);
    WriteU32(out.data() + kOffTag, header.tag);
    WriteU64(out.data() + kOffFrameIndex, header.frameIndex);
    WriteU64(out.data() + kOffReserved, header.reserved);
    return true;
}

std::optional<Header> DecodeHeader(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize) {
        return std::nullopt;
    }
    if (std::memcmp(bytes.data() + kOffMagic, kMagic, sizeof(kMagic)) != 0) {
        return std::nullopt;
    }

    Header header{};
    header.version = ReadU32(bytes.data() + kOffVersion);
    header.width = ReadU32(bytes.data() + kOffWidth);
    header.height = ReadU32(bytes.data() + kOffHeight);
    header.dxgiFormat = ReadU32(bytes.data() + kOffFormat);
    header.rowPitch = ReadU32(bytes.data() + kOffRowPitch);
    header.tag = ReadU32(bytes.data() + kOffTag);
    header.frameIndex = ReadU64(bytes.data() + kOffFrameIndex);
    header.reserved = ReadU64(bytes.data() + kOffReserved);

    if (!HeaderIsSelfConsistent(header)) {
        return std::nullopt;
    }

    // A capture interrupted mid-write is the expected corruption, so the
    // payload length is part of validation rather than something the caller is
    // trusted to check.
    const std::uint64_t expected = ExpectedFileSize(header);
    if (expected == 0 || bytes.size() != expected) {
        return std::nullopt;
    }
    return header;
}

std::uint64_t ExpectedFileSize(const Header& header) {
    if (!HeaderIsSelfConsistent(header)) {
        return 0;
    }
    return static_cast<std::uint64_t>(kHeaderSize) +
           static_cast<std::uint64_t>(header.rowPitch) * static_cast<std::uint64_t>(header.height);
}

Difference Compare(
    const Header& headerA,
    std::span<const std::uint8_t> payloadA,
    const Header& headerB,
    std::span<const std::uint8_t> payloadB,
    std::uint32_t perPixelEpsilon) {
    Difference difference{};

    if (headerA.width != headerB.width || headerA.height != headerB.height ||
        headerA.dxgiFormat != headerB.dxgiFormat) {
        return difference;
    }
    if (!HeaderIsSelfConsistent(headerA) || !HeaderIsSelfConsistent(headerB)) {
        return difference;
    }

    const std::uint64_t neededA =
        static_cast<std::uint64_t>(headerA.rowPitch) * static_cast<std::uint64_t>(headerA.height);
    const std::uint64_t neededB =
        static_cast<std::uint64_t>(headerB.rowPitch) * static_cast<std::uint64_t>(headerB.height);
    if (payloadA.size() < neededA || payloadB.size() < neededB) {
        return difference;
    }

    // Row pitches may differ between the two dumps: D3D11 chooses the staging
    // pitch, and it is not guaranteed stable across captures. Walking rows
    // separately rather than assuming a shared stride avoids a comparison that
    // silently comes out garbage when the driver pads differently.
    std::uint64_t sum = 0;
    std::uint32_t maximum = 0;
    std::uint64_t changed = 0;
    std::uint64_t pixels = 0;

    for (std::uint32_t y = 0; y < headerA.height; ++y) {
        const std::uint8_t* rowA = payloadA.data() + static_cast<std::size_t>(y) * headerA.rowPitch;
        const std::uint8_t* rowB = payloadB.data() + static_cast<std::size_t>(y) * headerB.rowPitch;
        for (std::uint32_t x = 0; x < headerA.width; ++x) {
            const std::uint8_t* pixelA = rowA + static_cast<std::size_t>(x) * 4u;
            const std::uint8_t* pixelB = rowB + static_cast<std::size_t>(x) * 4u;

            std::uint32_t worst = 0;
            // Alpha (index 3) is skipped -- see the header.
            for (int channel = 0; channel < 3; ++channel) {
                const int delta = static_cast<int>(pixelA[channel]) - static_cast<int>(pixelB[channel]);
                const std::uint32_t magnitude = static_cast<std::uint32_t>(delta < 0 ? -delta : delta);
                sum += magnitude;
                worst = std::max(worst, magnitude);
            }
            maximum = std::max(maximum, worst);
            if (worst > perPixelEpsilon) {
                ++changed;
            }
            ++pixels;
        }
    }

    difference.comparable = true;
    difference.pixelsCompared = pixels;
    difference.maxAbsolute = maximum;
    if (pixels > 0) {
        difference.meanAbsolute = static_cast<double>(sum) / static_cast<double>(pixels * 3ull);
        difference.changedPixelRatio = static_cast<double>(changed) / static_cast<double>(pixels);
    }
    return difference;
}

} // namespace preyvr::framedump
