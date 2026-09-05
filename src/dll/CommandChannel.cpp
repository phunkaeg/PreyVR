#include "CommandChannel.h"

#include "CameraEditHook.h"
#include "ConsoleBridgeWin32.h"
#include "FrameObserverHook.h"
#include "HandRigTakeover.h"
#include "HeadTrackingHook.h"
#include "Logger.h"
#include "NearViewStereo.h"
#include "WeaponAttachment.h"
#include "XrSessionHost.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_channel " + line);
}

std::atomic<bool> gRunning{false};
std::atomic<unsigned long long> gProcessed{0};
std::atomic<unsigned long long> gRejected{0};

std::filesystem::path CommandPath()
{
    return lifecycle::LogPath().parent_path() / L"commands.txt";
}

std::filesystem::path ResultPath()
{
    return lifecycle::LogPath().parent_path() / L"results.txt";
}

std::vector<std::string> Split(const std::string& line)
{
    std::vector<std::string> out;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) {
        out.push_back(token);
    }
    return out;
}

// Integer arguments are parsed defensively: a malformed number becomes the
// fallback rather than whatever `atoi` decides, so a typo cannot arm something
// with an unintended value.
bool ParseInt(const std::string& text, int& out)
{
    try {
        std::size_t consumed = 0;
        const int value = std::stoi(text, &consumed, 0);
        if (consumed != text.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseU64(const std::string& text, unsigned long long& out)
{
    try {
        std::size_t consumed = 0;
        const unsigned long long value = std::stoull(text, &consumed, 0);
        if (consumed != text.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

void WriteReport(std::ostringstream& out)
{
    out << "smoke=" << "n/a"
        << " observerFrames=" << ObservedFrameCount()
        << " xrSession=" << XrSessionStatusValue()
        << " xrFrames=" << XrSubmittedFrameCount()
        << " fovAgreed=" << DeclaredFovAgreeCount()
        << " fovDiverged=" << DeclaredFovDivergeCount()
        << " cameraEdit=" << CameraEditStatusValue()
        << " lastEye=" << LastRenderedEye()
        << " viewObserved=" << ViewHookObservedCount()
        << " viewApplied=" << ViewHookAppliedCount()
        << " posApplied=" << ViewPositionAppliedCount()
        << " posRefused=" << ViewPositionRefusedCount()
        << " posOffsetMm=" << ViewPositionOffsetMillimetres()
        << " nearApplied=" << NearViewAppliedCount()
        << " nearRefused=" << NearViewRefusedCount()
        << " nearNoEye=" << NearViewNoEyeCount()
        << " nearDeltaUm=" << NearViewLastDeltaMicrometres()
        << " handMatched=" << HandRigMatchedCount()
        << " handSkipped=" << HandRigSkippedCount()
        << " handApplied=" << HandRigAppliedCount()
        << " handRefused=" << HandRigRefusedCount()
        << " handSubtree=" << HandRigLastSubtreeSize()
        << " handRightMm=" << HandRigLastRightMillimetres()
        << " handLeftMm=" << HandRigLastLeftMillimetres()
        << " handNoPose=" << HandRigNoPoseCount()
        << " handJoints=" << HandRigJointCount()
        << " handLastCharacter=0x" << std::hex << HandRigLastCharacter() << std::dec
        << " weaponAttachment=0x" << std::hex << WeaponAttachmentPointer() << std::dec
        << " weaponMountMm=" << WeaponMountPositionMillimetres(0)
        << "," << WeaponMountPositionMillimetres(1)
        << "," << WeaponMountPositionMillimetres(2)
        << " weaponApplied=" << WeaponOffsetAppliedCount()
        << " weaponRefused=" << WeaponOffsetRefusedCount()
        << " channelProcessed=" << gProcessed.load(std::memory_order_relaxed)
        << " channelRejected=" << gRejected.load(std::memory_order_relaxed);
}

// One verb, one operation. Deliberately not a name-to-export lookup: that would
// be a call-anything primitive whose argument is a text file.
void Execute(const std::vector<std::string>& args, std::ostringstream& out)
{
    const std::string& verb = args[0];
    const auto arg = [&](std::size_t i, int fallback) {
        int value = fallback;
        if (i < args.size()) {
            ParseInt(args[i], value);
        }
        return value;
    };

    if (verb == "observer") {
        out << "observer result=" << SetFrameObserverEnabled(arg(1, 1));
    } else if (verb == "xr.runtime" && args.size() >= 2) {
        out << "xr.runtime result=" << SetXrRuntimeManifest(args[1].c_str());
    } else if (verb == "xr.srgb") {
        out << "xr.srgb result=" << SetXrPreferSrgbFormat(arg(1, 1));
    } else if (verb == "xr.start") {
        out << "xr.start result=" << StartXrSession();
    } else if (verb == "xr.native") {
        out << "xr.native result=" << SetNativeProjection(arg(1, 1));
    } else if (verb == "xr.stereo") {
        // Millimetres and degrees, so the file never carries a float.
        const float ipd = static_cast<float>(arg(1, 64)) / 1000.0f;
        const float halfFov = static_cast<float>(arg(2, 50));
        out << "xr.stereo result=" << SetSyntheticStereo(ipd, halfFov);
    } else if (verb == "xr.submit") {
        out << "xr.submit result=" << SetXrStereoSubmission(arg(1, 1));
    } else if (verb == "view.observe") {
        out << "view.observe result=" << SetViewHookObserving(arg(1, 1));
    } else if (verb == "view.recenter") {
        out << "view.recenter result=" << RecenterHeadTracking();
    } else if (verb == "view.apply") {
        out << "view.apply result=" << SetViewHookApplying(arg(1, 1));
    } else if (verb == "view.position") {
        out << "view.position result=" << SetViewPositionApplying(arg(1, 1));
    } else if (verb == "near.enable") {
        out << "near.enable result=" << SetNearViewStereo(arg(1, 1));
    } else if (verb == "near.halfipd") {
        out << "near.halfipd result=" << SetNearViewHalfIpdMillimetres(arg(1, 32));
    } else if (verb == "near.zero") {
        out << "near.zero result=" << SetNearViewZeroDeltaControl(arg(1, 1));
    } else if (verb == "hand.mode") {
        out << "hand.mode result=" << SetHandRigTakeoverMode(arg(1, 0));
    } else if (verb == "hand.joint") {
        out << "hand.joint result=" << SetHandRigJoint(arg(1, 0));
    } else if (verb == "hand.offset") {
        out << "hand.offset result="
            << SetHandRigOffsetMillimetres(arg(1, 0), arg(2, 0), arg(3, 0));
    } else if (verb == "hand.right") {
        out << "hand.right result=" << SetHandRigRightJoint(arg(1, 0));
    } else if (verb == "hand.left") {
        out << "hand.left result=" << SetHandRigLeftJoint(arg(1, 0));
    } else if (verb == "hand.drive") {
        out << "hand.drive result=" << SetHandRigControllerDrive(arg(1, 1));
    } else if (verb == "hand.calibrate") {
        out << "hand.calibrate result=" << CalibrateHandRig();
    } else if (verb == "hand.scale") {
        out << "hand.scale result=" << SetHandRigScalePercent(arg(1, 100));
    } else if (verb == "hand.character" && args.size() >= 2) {
        unsigned long long pointer = 0;
        if (!ParseU64(args[1], pointer)) {
            gRejected.fetch_add(1, std::memory_order_relaxed);
            out << "hand.character result=rejected detail=unparsable";
            return;
        }
        out << "hand.character result="
            << SetHandRigCharacterPtr(reinterpret_cast<void*>(pointer));
    } else if (verb == "console" && args.size() >= 2) {
        // Rebuilt from the tokens, then through the existing fail-closed
        // allowlist. This channel adds no reach to the console.
        std::string command = args[1];
        for (std::size_t i = 2; i < args.size(); ++i) {
            command += " " + args[i];
        }
        // **The console queue holds exactly one command.** Firing them back to
        // back returns `busy` for every one after the first, which is what
        // happened on this channel's first run: motion blur landed and the two
        // commands behind it were silently dropped with a code nobody read.
        //
        // The observer drains the queue once per frame, so retrying briefly is
        // enough. Bounded, because a game that has stopped rendering will never
        // drain it and this must not spin forever.
        DWORD result = QueueConsoleCommand(command.c_str());
        for (int attempt = 0; attempt < 40 && result == 4u; ++attempt) {
            Sleep(25);
            result = QueueConsoleCommand(command.c_str());
        }
        out << "console result=" << result
            << " command=\"" << command << "\"";
    } else if (verb == "weapon.observe") {
        out << "weapon.observe result=" << SetWeaponAttachmentObserving(arg(1, 1));
    } else if (verb == "weapon.offset") {
        out << "weapon.offset result="
            << SetWeaponOffsetMillimetres(arg(1, 0), arg(2, 0), arg(3, 0));
    } else if (verb == "weapon.apply") {
        out << "weapon.apply result=" << SetWeaponOffsetEnabled(arg(1, 1));
    } else if (verb == "report") {
        WriteReport(out);
    } else {
        // Reported, not ignored. A typo that silently does nothing is
        // indistinguishable from a mechanism that does not work.
        gRejected.fetch_add(1, std::memory_order_relaxed);
        out << "result=rejected verb=\"" << verb << "\"";
        return;
    }
    gProcessed.fetch_add(1, std::memory_order_relaxed);
}

DWORD WINAPI PollThread(LPVOID)
{
    const auto commands = CommandPath();
    const auto results = ResultPath();
    Log("result=0 detail=started commands=\"" + commands.string() +
        "\" results=\"" + results.string() + "\"");
    while (gRunning.load(std::memory_order_acquire)) {
        std::error_code error;
        if (std::filesystem::exists(commands, error) &&
            std::filesystem::file_size(commands, error) > 0) {
            std::vector<std::string> lines;
            {
                std::ifstream input(commands);
                std::string line;
                while (std::getline(input, line)) {
                    lines.push_back(line);
                }
            }
            // Truncate **before** executing, so a command that crashes or hangs
            // is not replayed on the next poll and on every poll after it.
            { std::ofstream clear(commands, std::ios::trunc); }

            std::ostringstream out;
            for (const std::string& line : lines) {
                const auto args = Split(line);
                if (args.empty() || args[0].empty() || args[0][0] == '#') {
                    continue;
                }
                Execute(args, out);
                out << '\n';
            }
            std::ofstream(results, std::ios::trunc) << out.str();
        }
        Sleep(200);
    }
    return 0;
}

} // namespace

DWORD StartCommandChannel()
{
    if (gRunning.exchange(true, std::memory_order_acq_rel)) {
        return 0;   // already running
    }
    const HANDLE thread = CreateThread(nullptr, 0, &PollThread, nullptr, 0, nullptr);
    if (thread == nullptr) {
        gRunning.store(false, std::memory_order_release);
        Log("result=failed detail=create_thread");
        return 1;
    }
    CloseHandle(thread);
    return 0;
}

unsigned long long CommandChannelProcessedCount()
{
    return gProcessed.load(std::memory_order_relaxed);
}

unsigned long long CommandChannelRejectedCount()
{
    return gRejected.load(std::memory_order_relaxed);
}

DWORD CommandChannelRunning() { return gRunning.load(std::memory_order_acquire) ? 1u : 0u; }

} // namespace preyvr::dll
