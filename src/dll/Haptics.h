#pragma once
#include "XrInput.h"
#include "preyvr/HapticPolicy.h"
#include <string>
namespace preyvr::dll {
void QueueHaptic(Hand,haptics::Event,const TrackingFrame&);
void SetHapticsEnabled(unsigned);
unsigned HapticsEnabled();
void SetHapticStrength(unsigned);
unsigned HapticStrength();
std::string HapticsReport();
}
