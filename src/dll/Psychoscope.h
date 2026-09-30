#pragma once
#include <string>
namespace preyvr::dll {
struct GameplayPoseFrame;
void UpdatePsychoscopeGesture(const GameplayPoseFrame&,bool tracking);
std::string PsychoscopeReport();
}
