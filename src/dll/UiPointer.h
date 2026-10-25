#pragma once
#include "preyvr/UiPanel.h"
#include "preyvr/UiPoke.h"
#include <windows.h>
#include <string>
namespace preyvr::dll {
struct TrackingFrame;
struct UiPointerVisual {
    Pose aim{};
    std::optional<ui::RayHit> hit;
    bool pressed=false;
    bool active=false;
    bool touch=false;
};
// XR-thread-owned gesture state, separate for the native and VR options panels.
struct UiPointerTracker {
    ui::Poke poke;
    std::uint64_t settings=0,clear=0,menu=0;
    unsigned hand=2;
    bool rayBlocked=false,wasTouch=false;
};
UiPointerVisual EvaluateUiPointer(const ui::Surface* surface,const TrackingFrame& frame,
    unsigned hand,std::uint64_t reference,std::uint64_t generation,UiPointerTracker& tracker);
// Frame-service producer; main-thread consumer. No native UI call on the XR thread.
// The surface shows the frame's rows top..top+height (the menu's band): the
// pointer is reported in whole-frame pixels.
UiPointerVisual PublishUiPointer(const ui::Surface* surface,unsigned width,unsigned height,
                                unsigned long long reference,unsigned top=0);
void DrainUiPointer();
void ClearUiPointer();
DWORD SetUiPointerHand(unsigned hand); // 0 left, 1 right, 2 disabled
unsigned UiPointerHand();
DWORD SetUiPokeEnabled(unsigned enabled);
unsigned UiPokeEnabled();
DWORD SetUiPokeDistanceCm(unsigned cm);
unsigned UiPokeDistanceCm();
DWORD SetUiPokeTipMm(unsigned mm);
unsigned UiPokeTipMm();
std::uint64_t UiPokeSettingsGeneration();
std::string UiPointerReport();
}
