#pragma once
#include "preyvr/VrOptions.h"
#include "preyvr/UiPanel.h"
#include "UiPointer.h"
#include "XrInput.h"
#include <string>
namespace preyvr::dll {
// XR frame thread owns the menu state. Worker requests are atomic mailboxes.
bool ProcessVrOptionsInput(const TrackingFrame&);
UiPointerVisual OptionsPointer(const ui::Surface&);
void SetVrOptionsSurface(const ui::Surface* surface,std::uint64_t reference);
bool VrOptionsOpen();
bool VrOptionsReady();
bool VrOptionsInputOwned();
void RequestVrOptions(bool open);
void ResetVrOptionsSession(); // XR teardown
void LoadVrOptions(); // command worker, once after launch defaults have applied
void ServiceVrOptions(); // worker: settings application and disk I/O
options::Values VrOptionsValues();
unsigned VrOptionsPage();
unsigned VrOptionsRow();
bool VrOptionsSaveFailed();
bool PsychoscopeGestureEnabled();
bool HolstersEnabled();
bool WristDisplayEnabled();
unsigned WristSizePercent();
std::string VrOptionsReport();
}
