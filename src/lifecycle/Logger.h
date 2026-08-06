#pragma once

#include <filesystem>
#include <string_view>

namespace preyvr::lifecycle {

std::filesystem::path LogPath();
void ResetLog();
void Log(std::string_view message);

} // namespace preyvr::lifecycle
