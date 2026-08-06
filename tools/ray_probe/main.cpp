#include "preyvr/AimState.h"

#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace {

bool ParseFloat(const char* text, float& value)
{
    char* end = nullptr;
    value = std::strtof(text, &end);
    return end != text && *end == '\0';
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 5) {
        std::cerr << "usage: preyvr_ray_probe <x> <y> <z> <yaw_degrees>\n";
        return 2;
    }

    preyvr::CachedReticleState source{};
    float yawDegrees = 0.0f;
    if (!ParseFloat(argv[1], source.direction.x) ||
        !ParseFloat(argv[2], source.direction.y) ||
        !ParseFloat(argv[3], source.direction.z) ||
        !ParseFloat(argv[4], yawDegrees)) {
        std::cerr << "error: every argument must be a finite decimal float\n";
        return 2;
    }
    source.screenPosition = {0.5f, 0.5f};

    const auto probe = preyvr::MakeBoundedYawProbe(source, yawDegrees);
    if (!probe) {
        std::cerr << "error: direction must be normalized and yaw must be within 0.1 to 15 degrees\n";
        return 1;
    }
    const auto bytes = preyvr::EncodeDirectionBytes(probe->direction);
    if (!bytes) {
        std::cerr << "error: rotated direction could not be encoded\n";
        return 1;
    }

    std::cout << std::fixed << std::setprecision(9)
              << "probe_direction x=" << probe->direction.x
              << " y=" << probe->direction.y
              << " z=" << probe->direction.z << '\n'
              << "probe_yaw_degrees="
              << preyvr::DirectionYawDeltaDegrees(source.direction, probe->direction) << '\n'
              << "probe_angular_delta_degrees="
              << preyvr::DirectionAngularDeltaDegrees(source, *probe) << '\n'
              << "probe_bytes=";
    for (std::size_t index = 0; index < bytes->size(); ++index) {
        if (index != 0) {
            std::cout << ' ';
        }
        std::cout << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned>((*bytes)[index]);
    }
    std::cout << '\n';
    return 0;
}
