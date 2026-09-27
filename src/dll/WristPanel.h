#pragma once
#include "preyvr/BodyEquipment.h"
#include <vector>
#include <cstdint>
namespace preyvr::dll {
inline constexpr unsigned WristWidth=720,WristHeight=400;
std::vector<std::uint8_t> DrawWristPanel(const equipment::Vitals&);
}
