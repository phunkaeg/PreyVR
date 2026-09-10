#pragma once
#include <cstdint>
#include <vector>
namespace preyvr::dll {
inline constexpr unsigned kGuideWidth=1024,kGuideHeight=160;
// BGRA, premultiplied source alpha. Built once per session on the render thread.
std::vector<std::uint8_t> MakeMenuGuide();
}
