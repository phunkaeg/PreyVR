#pragma once
#include "preyvr/VrOptions.h"
#include <vector>
namespace preyvr::dll {
std::vector<std::uint8_t> DrawVrOptions(const options::Values&,unsigned page,unsigned selected,bool saveFailed,std::wstring_view setupMessage={});
}
