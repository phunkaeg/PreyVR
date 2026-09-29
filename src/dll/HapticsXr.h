#pragma once
#include "Haptics.h"
#include <openxr/openxr.h>
namespace preyvr::dll {
// Only the OpenXR frame owner calls these functions.
void ServiceHaptics(XrSession,XrAction,const std::array<XrPath,2>&,const TrackingFrame&,bool focused);
void ResetHaptics(XrSession,XrAction,const std::array<XrPath,2>&);
}
