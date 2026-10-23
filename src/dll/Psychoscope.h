#pragma once
#include <string>
namespace preyvr::dll {
struct GameplayPoseFrame;
void UpdatePsychoscopeGesture(const GameplayPoseFrame&,bool tracking);
// The gesture is on and the left hand is in its zone: a squeeze there is the
// psychoscope's, not a use (InteractionUse.h).
bool PsychoscopeZoneHasLeftHand();
std::string PsychoscopeReport();
}
