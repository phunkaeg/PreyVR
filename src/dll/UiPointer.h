#pragma once
#include "preyvr/UiPanel.h"
#include <windows.h>
#include <string>
namespace preyvr::dll {
struct UiPointerVisual {
    Pose aim{};
    std::optional<ui::RayHit> hit;
    bool pressed=false;
    bool active=false;
};
// Frame-service producer; main-thread consumer. No native UI call on the XR thread.
// The surface shows the frame's rows top..top+height (the menu's band): the
// pointer is reported in whole-frame pixels.
UiPointerVisual PublishUiPointer(const ui::Surface* surface,unsigned width,unsigned height,
                                unsigned long long reference,unsigned top=0);
void DrainUiPointer();
void ClearUiPointer();
DWORD SetUiPointerHand(unsigned hand); // 0 left, 1 right, 2 disabled
unsigned UiPointerHand();
std::string UiPointerReport();
}
