#include "HotkeyBridge.h"

#include "HeadTrackingHook.h"

#include "CameraEditHook.h"
#include "ConsoleBridgeWin32.h"
#include "Logger.h"
#include "XrSessionHost.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <sstream>
#include <string>
#include <thread>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_hotkey " + line);
}

// Coarse enough that consecutive steps are actually distinguishable in a
// headset. A finer table would produce a lot of presses that change nothing a
// human can report, which is worse than offering fewer options.
constexpr std::array<float, 7> kIpdSteps = {
    0.045f, 0.052f, 0.058f, 0.064f, 0.070f, 0.076f, 0.085f,
};
constexpr int kDefaultIpdIndex = 3;   // 0.064

std::atomic<bool> gRunning{false};
std::atomic<int> gIpdIndex{kDefaultIpdIndex};
std::atomic<int> gAaMode{0};
std::atomic<bool> gEyeSwap{false};
std::atomic<unsigned int> gPressCount{0};
std::thread gThread;

bool Chord()
{
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
           (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
}

void ApplyIpd()
{
    const float ipd = kIpdSteps[static_cast<std::size_t>(gIpdIndex.load(std::memory_order_relaxed))];
    // 50.0 is the synthetic half-FOV. It is unused once native projection is on;
    // it is passed because the arming call requires a value in range, not
    // because it affects what is rendered.
    SetSyntheticStereo(ipd, 50.0f);
    std::ostringstream line;
    line << "result=0 detail=ipd value=" << ipd;
    Log(line.str());
}

void ApplyAa()
{
    const int mode = gAaMode.load(std::memory_order_relaxed);
    std::ostringstream command;
    command << "r_AntialiasingMode " << mode;
    QueueConsoleCommand(command.str().c_str());
    Log("result=0 detail=aa mode=" + std::to_string(mode));
}

// Position scale, in thousandths. 1000 is the measured unitsPerMetre; the
// neighbours exist so a wrong measurement shows up as "one of these feels right
// and 1000 does not", which is a far better signal than a slider.
constexpr std::array<int, 7> kPositionScales = {500, 750, 1000, 1250, 1500, 2000, 3280};
// Index 2 == 1000 == measured. Starting anywhere else would bias the very
// judgement this lever exists to make.
std::atomic<int> gPositionScaleIndex{2};
std::atomic<bool> gPositionOn{false};

void ApplyPositionScale()
{
    const int milli = kPositionScales[static_cast<std::size_t>(
        gPositionScaleIndex.load(std::memory_order_relaxed))];
    SetViewPositionScaleMilli(static_cast<unsigned int>(milli));
    Log("result=0 detail=position_scale milli=" + std::to_string(milli));
}

void Poll()
{
    // Edge-detected: a held key must not repeat, or one press walks the whole
    // table before the wearer has looked at anything.
    std::array<bool, 9> was{};
    // **Not the arrow keys.** Ctrl+Alt+Arrow is Intel's display-rotation
    // shortcut on machines where that driver's hotkeys are enabled, so cycling
    // the eye offset could rotate the desktop out from under a wearer who cannot
    // see it happening. PageUp/PageDown and Home/End are claimed by nothing.
    //
    // **Not Delete.** Ctrl+Alt+Delete is the Windows secure attention sequence:
    // it cannot be captured, and it would throw a wearer who cannot see the
    // screen onto the security desktop. Letters for the position lane instead.
    const int keys[9] = {VK_PRIOR, VK_NEXT, VK_HOME, VK_END, 'E', VK_BACK,
                         'P', VK_INSERT, 'O'};

    while (gRunning.load(std::memory_order_acquire)) {
        const bool chord = Chord();
        for (int i = 0; i < 9; ++i) {
            const bool down = chord && (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
            const bool pressed = down && !was[static_cast<std::size_t>(i)];
            was[static_cast<std::size_t>(i)] = down;
            if (!pressed) {
                continue;
            }
            gPressCount.fetch_add(1, std::memory_order_relaxed);
            switch (i) {
                case 0:
                    gIpdIndex.store(
                        std::min<int>(gIpdIndex.load(std::memory_order_relaxed) + 1,
                                      static_cast<int>(kIpdSteps.size()) - 1),
                        std::memory_order_relaxed);
                    ApplyIpd();
                    break;
                case 1:
                    gIpdIndex.store(
                        std::max<int>(gIpdIndex.load(std::memory_order_relaxed) - 1, 0),
                        std::memory_order_relaxed);
                    ApplyIpd();
                    break;
                case 2:
                    gAaMode.store(std::min<int>(gAaMode.load(std::memory_order_relaxed) + 1, 3),
                                  std::memory_order_relaxed);
                    ApplyAa();
                    break;
                case 3:
                    gAaMode.store(std::max<int>(gAaMode.load(std::memory_order_relaxed) - 1, 0),
                                  std::memory_order_relaxed);
                    ApplyAa();
                    break;
                case 4: {
                    const bool next = !gEyeSwap.load(std::memory_order_relaxed);
                    gEyeSwap.store(next, std::memory_order_relaxed);
                    SetXrSwapEyes(next ? 1u : 0u);
                    Log(std::string("result=0 detail=swap_eyes enabled=") + (next ? "1" : "0"));
                    break;
                }
                case 5:
                    // Panic. Ordered so the image stops being stereo before the
                    // camera stops being edited: the reverse would show a frame
                    // of mono image underneath a stereo declaration.
                    SetXrStereoSubmission(0u);
                    SetSyntheticStereo(0.0f, 0.0f);
                    // Position last but always: a camera in the wrong place is
                    // the least recoverable of these by looking away.
                    SetViewPositionApplying(0u);
                    gPositionOn.store(false, std::memory_order_relaxed);
                    Log("result=0 detail=panic_disarm");
                    break;
                case 6: {
                    const bool next = !gPositionOn.load(std::memory_order_relaxed);
                    gPositionOn.store(next, std::memory_order_relaxed);
                    SetViewPositionApplying(next ? 1u : 0u);
                    Log(std::string("result=0 detail=position_lane enabled=") +
                        (next ? "1" : "0"));
                    break;
                }
                case 7:
                    gPositionScaleIndex.store(
                        std::min<int>(gPositionScaleIndex.load(std::memory_order_relaxed) + 1,
                                      static_cast<int>(kPositionScales.size()) - 1),
                        std::memory_order_relaxed);
                    ApplyPositionScale();
                    break;
                case 8:
                    gPositionScaleIndex.store(
                        std::max<int>(gPositionScaleIndex.load(std::memory_order_relaxed) - 1, 0),
                        std::memory_order_relaxed);
                    ApplyPositionScale();
                    break;
                default:
                    break;
            }
        }
        Sleep(30);
    }
}

} // namespace

DWORD SetHotkeysEnabled(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on == gRunning.load(std::memory_order_acquire)) {
        return 0;
    }
    if (on) {
        gPressCount.store(0, std::memory_order_relaxed);
        gIpdIndex.store(kDefaultIpdIndex, std::memory_order_relaxed);
        gRunning.store(true, std::memory_order_release);
        gThread = std::thread(Poll);
        Log("result=0 detail=enabled");
    } else {
        gRunning.store(false, std::memory_order_release);
        if (gThread.joinable()) {
            gThread.join();
        }
        Log("result=0 detail=disabled");
    }
    return 0;
}

DWORD HotkeyIpdTenthsMm()
{
    const float ipd = kIpdSteps[static_cast<std::size_t>(gIpdIndex.load(std::memory_order_relaxed))];
    return static_cast<DWORD>((ipd * 10000.0f) + 0.5f);
}

DWORD HotkeyAaMode()
{
    return static_cast<DWORD>(gAaMode.load(std::memory_order_relaxed));
}

DWORD HotkeyEyeSwap()
{
    return gEyeSwap.load(std::memory_order_relaxed) ? 1u : 0u;
}

DWORD HotkeyPressCount()
{
    return gPressCount.load(std::memory_order_relaxed);
}

} // namespace preyvr::dll
