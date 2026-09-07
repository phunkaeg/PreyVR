#pragma once
#include "preyvr/VrMath.h"
#include <cmath>
#include <cstring>
#include <optional>
#include <span>

namespace preyvr {
struct FiringPosition {
    Vec3 origin{};
    unsigned int cameraFallback = 0;
};

// 0x1694BC0 result: flag at +0, Vec3 at +4, total 16 bytes. Padding is
// unspecified. Decode a copied buffer; this function never reads engine memory.
inline std::optional<FiringPosition> DecodeFiringPosition(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < 16 || bytes[0] > 1) { return std::nullopt; }
    FiringPosition value{};
    value.cameraFallback = bytes[0];
    std::memcpy(&value.origin, bytes.data() + 4, sizeof(value.origin));
    if (!std::isfinite(value.origin.x) || !std::isfinite(value.origin.y) || !std::isfinite(value.origin.z)) {
        return std::nullopt;
    }
    return value;
}
} // namespace preyvr
