// Production modal-state publication with synthetic visibility results. This
// does not fake the concrete Steam UI vtables or run a Prey process.
#include "../src/dll/HudBridge.cpp"
#include <cstdlib>
#include <iostream>

namespace {
bool optionsOwn = false;
void Check(bool value, const char* why)
{
    if (!value) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
}
namespace preyvr::lifecycle { void Log(std::string_view) {} }
namespace preyvr::dll {
bool VrOptionsInputOwned() { return optionsOwn; }
DWORD XrResolutionChain(unsigned field) { return field == 4 ? 2016u : 2160u; }
DWORD EnsureRenderHookInstalled() { return 1; }
DWORD EnsureInventoryPointerCapture() { return 1; }
void BeginInventoryPointerEvent(int) {}
void EndInventoryPointerEvent(int) {}
}

int main()
{
    using namespace preyvr;
    using namespace preyvr::dll;
    PublishModalSample(0, true);
    Check(!HudMenuIsOpen() && HudGameplayInputAllowed(), "known empty sample admits gameplay");
    const auto firstEpoch = HudMenuEpoch();
    PublishModalSample(kPdaModal, true);
    Check(HudMenuIsOpen() && HudInventoryIsOpen() && !HudHackingIsOpen(), "PDA positive control");
    const auto pdaEpoch = HudMenuEpoch();
    Check(pdaEpoch == firstEpoch + 1, "opening modal changes epoch");
    PublishModalSample(kPdaModal, true);
    Check(HudMenuEpoch() == pdaEpoch, "unchanged content preserves epoch");
    PublishModalSample(kHackingModal, true);
    Check(HudMenuIsOpen() && !HudInventoryIsOpen() && HudHackingIsOpen(), "hacking is modal and not inventory");
    Check(HudMenuEpoch() == pdaEpoch + 1, "PDA to hacking invalidates input despite staying modal");
    PublishModalSample(kHackingModal | kPdaModal, true);
    Check(HudHackingIsOpen() && !HudInventoryIsOpen(), "hacking can own its PDA backing");
    const auto hackingEpoch = HudMenuEpoch();
    PublishModalSample(kHackingModal | 1ull, true); // pause overlay
    Check(HudMenuIsOpen() && !HudHackingIsOpen(), "pause covers still-visible hacking");
    Check(HudMenuEpoch() == hackingEpoch + 1, "overlay transition invalidates old hacking input");
    PublishModalSample(kHackingModal | (1ull << 7), true); // native dialog
    Check(!HudHackingIsOpen(), "dialog owns input over hacking");
    PublishModalSample(kHackingModal, false);
    Check(HudMenuIsOpen() && !HudHackingIsOpen() && !HudInventoryIsOpen(), "unverified sibling visibility refuses exclusive consumers");
    PublishModalSample(kHackingModal, true);
    gMenuStamp.store(MonotonicNanoseconds() - 300000000ull);
    Check(!HudMenuStateKnown() && !HudHackingIsOpen(), "stale hacking sample refuses input");
    const auto epochBeforeFailedPoll = HudMenuEpoch();
    Check(HudRefreshMenuOpenState() == 4 && HudMenuIsOpen() && HudMenuEpoch() == epochBeforeFailedPoll,
          "missing target poll retains previous modal state");
    PublishModalSample(0, true);
    optionsOwn = true;
    Check(!HudGameplayInputAllowed(), "VR options retain independent input ownership");
    std::cout << "hud_modal ok: content epochs, hacking ownership, coverings, freshness and failed-poll control\n";
}
