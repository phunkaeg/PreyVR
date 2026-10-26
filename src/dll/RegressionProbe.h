#pragma once
#include <string>
#include <string_view>
namespace preyvr::dll {
// Existing publications and Win32 module identity only; no engine calls/setters.
// Correlation token is mandatory so stale results cannot satisfy a runner wait.
std::string RegressionStateJson(std::string_view request);
}
