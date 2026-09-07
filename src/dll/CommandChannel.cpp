#include "CommandChannel.h"

#include "AimTakeover.h"
#include "AnimIkTakeover.h"
#include "CameraEditHook.h"
#include "ConsoleBridgeWin32.h"
#include "FrameObserverHook.h"
#include "HandRigTakeover.h"
#include "HeadTrackingHook.h"
#include "Logger.h"
#include "NearViewStereo.h"
#include "RenderFrame.h"
#include "InputPost.h"
#include "MoveLane.h"
#include "WeaponAttachment.h"
#include "XrInput.h"
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
        << " viewPoseFallback=" << HeadTrackingPoseReadFallbacks()
        << " posApplied=" << ViewPositionAppliedCount()
        << " posRefused=" << ViewPositionRefusedCount()
        << " posOffsetMm=" << ViewPositionOffsetMillimetres()
        << " nearApplied=" << NearViewAppliedCount()
        << " nearRefused=" << NearViewRefusedCount()
        << " nearNoEye=" << NearViewNoEyeCount()
        << " nearReentered=" << NearViewReenteredCount()
        << " nearAlreadyOffset=" << NearViewAlreadyOffsetCount()
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
        << " handMode=" << HandRigMode()
        << " handDriveArmed=" << HandRigControllerDriveArmed()
        << " handCalibrated=" << HandRigCalibrationDone()
        << " handRightJoint=" << HandRigSelectedRightJoint()
        << " handLeftJoint=" << HandRigSelectedLeftJoint()
        << " handWristArmed=" << HandRigWristDriveArmed()
        << " handWristApplied=" << HandRigWristAppliedCount()
        << " handTurnYaw=" << HandRigTurnYawDeciDegrees()
        << " ikMode=" << AnimIkMode()
        << " ikHooked=" << AnimIkHooked()
        << " ikOwner=0x" << std::hex << AnimIkOwnerCharacter() << std::dec
        << " ikEquipGen=" << AnimIkOwnerGeneration() << " ikPoseSeq=" << AnimIkPoseSequence()
        << " ikNoOwner=" << AnimIkNoOwner() << " ikBusy=" << AnimIkBusy()
        << " ikCalls=" << AnimIkCalls()
        << " ikMatched=" << AnimIkMatched()
        << " ikRig=0x" << std::hex << AnimIkRigSkeleton() << std::dec
        << " ikJoints=" << AnimIkRigJoints()
        << " ikGate=" << AnimIkGate()
        << " ikCvar=" << AnimIkCvar()
        << " ikTargetR=" << AnimIkTargetJoint(0) << " ikWeightR=" << AnimIkWeightJoint(0)
        << " ikLimbEndR=" << AnimIkLimbEnd(0) << " ikLimbTagR=0x" << std::hex << AnimIkLimbTag(0) << std::dec
        << " ikTargetL=" << AnimIkTargetJoint(1) << " ikWeightL=" << AnimIkWeightJoint(1)
        << " ikLimbEndL=" << AnimIkLimbEnd(1)
        << " ikWrittenR=" << AnimIkWritten(0) << " ikWrittenL=" << AnimIkWritten(1)
        << " ikNoPose=" << AnimIkNoPose() << " ikClamped=" << AnimIkClamped()
        << " ikReach=" << AnimIkReachPercent()
        << " ikCalR=" << AnimIkCalibrated(0) << " ikCalL=" << AnimIkCalibrated(1)
        << " ikLocMm=" << AnimIkLocationMillimetres(0) << "," << AnimIkLocationMillimetres(1)
        << "," << AnimIkLocationMillimetres(2)
        << " ikLocYawMdeg=" << AnimIkLocationYawMilliDegrees()
        << " ikGoalMm=" << AnimIkLastGoalMillimetres(0) << "," << AnimIkLastGoalMillimetres(1)
        << "," << AnimIkLastGoalMillimetres(2)
        << " handZeroRightMm=" << HandRigZeroRightMm(0) << "," << HandRigZeroRightMm(1)
        << "," << HandRigZeroRightMm(2)
        << " handWorldRightMm=" << HandRigWorldRightMm(0) << "," << HandRigWorldRightMm(1)
        << "," << HandRigWorldRightMm(2)
        << " handCalibYawMdeg=" << HandRigCalibrationYawMilli()
        << " handLastYawMdeg=" << HandRigLastYawMilli()
        << " handLastCharacter=0x" << std::hex << HandRigLastCharacter() << std::dec
        << " weaponAttachment=0x" << std::hex << WeaponAttachmentPointer() << std::dec
        << " weaponBone=" << WeaponAttachmentJointIndex()
        << " weaponSim=0x" << std::hex << WeaponAttachmentSimulationFlags() << std::dec
        << WeaponMuzzleAlignmentReport()
        << " aimOriginApplied=" << AimOriginAppliedCount()
        << " aimBodyYaw=" << AimBodyYawEnabled()
        << " camYawMdeg=" << AimCameraYawMilliDegrees()
        << " headYawMdeg=" << AimHeadYawMilliDegrees()
        << " playYawMdeg=" << AimPlaySpaceYawMilliDegrees()
        << " yawHeld=" << AimHeadYawHeldCount()
        << " yawUnavailable=" << AimHeadYawUnavailableCount()
        << " weaponMountMm=" << WeaponMountPositionMillimetres(0)
        << "," << WeaponMountPositionMillimetres(1)
        << "," << WeaponMountPositionMillimetres(2)
        << " weaponApplied=" << WeaponOffsetAppliedCount()
        << " weaponRefused=" << WeaponOffsetRefusedCount()
        << " weaponRotApplied=" << WeaponRotationAppliedCount()
        << " weaponRotNoPose=" << WeaponRotationNoPoseCount()
        << " weaponRotDrive=" << WeaponRotationDriveArmed()
        << " weaponRotCalib=" << WeaponRotationCalibrated()
        << " weaponBaseline=" << WeaponBaselineCaptured()
        << " weaponAimUsable=" << WeaponAimPoseUsable()
        << " aimApplied=" << AimTakeoverAppliedCount()
        << " aimNoPose=" << AimTakeoverRejectedNoPose()
        << " aimNoPlayer=" << AimTakeoverRejectedNoPlayer()
        << " aimCompose=" << AimTakeoverRejectedCompose()
        << " aimNativeMagMilli=" << AimTakeoverNativeDirectionMagnitude()
        << " frameCaptures=" << RenderFrameCaptureCount()
        << " frameTracked=" << RenderFrameTrackedCharacters()
        << " frameOverrideApplied=" << RenderFrameOverrideAppliedCount()
        << " frameOverrideRefused=" << RenderFrameOverrideRefusedCount()
        << " menuActions=" << MenuNavigationActionCount()
        << " inputPosted=" << InputPostCount()
        << " moveMode=" << MoveLaneMode()
        << " moveHooked=" << MoveLaneHooked()
        << " moveOursX=" << MoveLaneOursX() << " moveOursY=" << MoveLaneOursY()
        << " moveNative=" << MoveLaneNative()
        << " movePosted=" << MoveLanePosted() << " moveDropped=" << MoveLaneDropped()
        << " moveInputObj=0x" << std::hex << MoveLaneInputObject() << std::dec
        << " moveAxisMilli=" << MoveLaneAxisMilli(0) << "," << MoveLaneAxisMilli(1)
        << " moveCinematic=" << MoveLaneCinematicGate()
        << " inputDropped=" << InputQueueDroppedCount()
        << " inputDrainThread=" << InputDrainThreadId()
        << " channelProcessed=" << gProcessed.load(std::memory_order_relaxed)
        << " channelRejected=" << gRejected.load(std::memory_order_relaxed);
}

// **A status is not a result code, and printing one as the other is how a
// working session gets reported as a failure.** `xr.start` returns
// `XrSessionStatus`, where 1 means *running*; `observer` returns
// `FrameObserverRuntimeStatus`, where 2 means *enabled*. Both were read as
// errors during a live run, and a correction had to be issued for each. These
// print the name alongside the number so the reading needs no lookup.
const char* XrStatusName(DWORD value)
{
    switch (value) {
        case 0: return "idle";
        case 1: return "running";
        case 2: return "adapter_mismatch";
        case 3: return "unavailable";
        case 4: return "failed";
        case 5: return "stopped";
        default: return "unknown";
    }
}

const char* ObserverStatusName(DWORD value)
{
    switch (value) {
        case 0: return "unavailable";
        case 1: return "ready";
        case 2: return "enabled";
        case 3: return "failed";
        default: return "unknown";
    }
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
        const DWORD status = SetFrameObserverEnabled(arg(1, 1));
        out << "observer status=" << ObserverStatusName(status) << "(" << status << ")";
    } else if (verb == "xr.runtime" && args.size() >= 2) {
        out << "xr.runtime result=" << SetXrRuntimeManifest(args[1].c_str());
    } else if (verb == "xr.srgb") {
        out << "xr.srgb result=" << SetXrPreferSrgbFormat(arg(1, 1));
    } else if (verb == "xr.start") {
        const DWORD status = StartXrSession();
        out << "xr.start status=" << XrStatusName(status) << "(" << status << ")";
    } else if (verb == "xr.native") {
        out << "xr.native result=" << SetNativeProjection(arg(1, 1));
    } else if (verb == "xr.stereo") {
        // Millimetres and degrees, so the file never carries a float.
        const float ipd = static_cast<float>(arg(1, 64)) / 1000.0f;
        const float halfFov = static_cast<float>(arg(2, 50));
        out << "xr.stereo result=" << SetSyntheticStereo(ipd, halfFov);
    } else if (verb == "xr.submit") {
        const DWORD status = SetXrStereoSubmission(arg(1, 1));
        out << "xr.submit status=" << XrStatusName(status) << "(" << status << ")";
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
    } else if (verb == "near.lineage") {
        out << "near.lineage result=" << ArmNearViewLineage(arg(1, 1));
    } else if (verb == "near.dump") {
        out << "near.dump result=" << DumpNearViewLineage();
    } else if (verb == "near.zero") {
        out << "near.zero result=" << SetNearViewZeroDeltaControl(arg(1, 1));
    } else if (verb == "ik.mode") {
        // 0 off, 1 observe (identify the rig, read its ADIK table, write
        // nothing), 2 apply. Mutually exclusive with hand.mode 2.
        const int requested = arg(1, 0);
        out << "ik.mode result=" << SetAnimIkMode(requested) << " mode=" << requested
            << (requested == 0 ? "(off)" : requested == 1 ? "(observe)" : requested == 2 ? "(apply)" : "(unknown)");
    } else if (verb == "ik.test") {
        out << "ik.test result=" << SetAnimIkTestOffsetMillimetres(arg(1, 0), arg(2, 0), arg(3, 0));
    } else if (verb == "ik.drive") {
        out << "ik.drive result=" << SetAnimIkControllerDrive(arg(1, 1));
    } else if (verb == "ik.calibrate") {
        out << "ik.calibrate result=" << CalibrateAnimIk();
    } else if (verb == "ik.joints") {
        out << "ik.joints result=" << SetAnimIkJointSignature(arg(1, 101));
    } else if (verb == "ik.reach") {
        out << "ik.reach result=" << SetAnimIkReachPercent(arg(1, 100))
            << " percent=" << AnimIkReachPercent();
    } else if (verb == "ik.hands") {
        out << "ik.hands result=" << SetAnimIkHands(arg(1, 1));
    } else if (verb == "ik.dump") {
        out << "ik.dump result=" << DumpAnimIk();
    } else if (verb == "hand.mode") {
        const int requested = arg(1, 0);
        const DWORD result = SetHandRigTakeoverMode(requested);
        // Named, because "1" reads like "on" and is actually the control that
        // deliberately changes nothing.
        const char* name = requested == 0 ? "off"
                         : requested == 1 ? "passthrough_changes_nothing"
                         : requested == 2 ? "apply"
                                          : "unknown";
        out << "hand.mode result=" << result << " mode=" << requested << "(" << name << ")";
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
    } else if (verb == "hand.wrist") {
        out << "hand.wrist result=" << SetHandRigWristDrive(arg(1, 1));
    } else if (verb == "hand.turnyaw") {
        // Tenths of a degree, so 1800 is a half turn. Reported back so a wearer
        // reading the channel can see what is actually set.
        out << "hand.turnyaw result=" << SetHandRigTurnYaw(arg(1, 0))
            << " deciDegrees=" << HandRigTurnYawDeciDegrees();
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
    } else if (verb == "aim.bodyyaw") {
        // 1 rotates head-relative offsets by the BODY yaw (camera - head), 0 by
        // the camera yaw. 0 makes the hand and weapon swing with the headset.
        out << "aim.bodyyaw result=" << SetAimBodyYaw(arg(1, 1))
            << " value=" << AimBodyYawEnabled();
    } else if (verb == "aim.origin") {
        // Diagnostic controller-aim origin; this does not establish a muzzle
        // transform or remove projectile convergence from the authored helper.
        out << "aim.origin result=" << SetAimOriginFromHand(arg(1, 1));
    } else if (verb == "aim.enable") {
        // The detached-aim lane, reachable from the channel at last. H-004 is
        // reproduced against two native consumers -- the wrench contact moved
        // 0.127165 units and the interaction selector committed a different
        // entity -- but both used synthetic angle offsets driven from a
        // debugger. This is what lets a *controller* drive it.
        out << "aim.enable result=" << SetAimTakeoverEnabled(arg(1, 1))
            << " applied=" << AimTakeoverAppliedCount()
            << " nativeMagMilli=" << AimTakeoverNativeDirectionMagnitude();
    } else if (verb == "weapon.observe") {
        out << "weapon.observe result=" << SetWeaponAttachmentObserving(arg(1, 1));
    } else if (verb == "weapon.offset") {
        out << "weapon.offset result="
            << SetWeaponOffsetMillimetres(arg(1, 0), arg(2, 0), arg(3, 0));
    } else if (verb == "weapon.apply") {
        out << "weapon.apply result=" << SetWeaponOffsetEnabled(arg(1, 1));
    } else if (verb == "weapon.rotate") {
        out << "weapon.rotate result=" << SetWeaponRotationDrive(arg(1, 1));
    } else if (verb == "weapon.calibrate") {
        out << "weapon.calibrate result=" << CalibrateWeaponRotation();
    } else if (verb == "frame.capture") {
        out << "frame.capture result=" << SetRenderFrameCapture(arg(1, 1));
    } else if (verb == "frame.character" && args.size() >= 2) {
        unsigned long long pointer = 0;
        if (!ParseU64(args[1], pointer)) {
            out << "frame.character result=2 detail=parse";
        } else {
            out << "frame.character result=" << SetRenderFrameOverrideCharacter(pointer);
        }
    } else if (verb == "frame.offset") {
        out << "frame.offset result="
            << SetRenderFrameOffsetMillimetres(arg(1, 0), arg(2, 0), arg(3, 0));
    } else if (verb == "frame.apply") {
        out << "frame.apply result=" << SetRenderFrameOverrideEnabled(arg(1, 1))
            << " applied=" << RenderFrameOverrideAppliedCount()
            << " refused=" << RenderFrameOverrideRefusedCount();
    } else if (verb == "frame.propagate") {
        out << "frame.propagate result="
            << SetRenderFrameOverridePropagates(arg(1, 1));
    } else if (verb == "frame.near") {
        // Dumps the capture table with RenderCHR's own near verdict per slot
        // (R-089), so identifying the near character no longer needs an injector
        // attached to the running game.
        out << "frame.near result=0";
        unsigned int listed = 0;
        for (unsigned int i = 0; i < 8; ++i) {
            unsigned long long character = 0;
            bool nearest = false;
            if (!RenderFrameSlot(i, &character, &nearest)) {
                continue;
            }
            out << " slot" << i << "=0x" << std::hex << character << std::dec
                << ",near=" << (nearest ? 1 : 0);
            ++listed;
        }
        out << " listed=" << listed;
    } else if (verb == "move.mode") {
        // 0 off, 1 observe (hook the analog handlers, attribute every call,
        // post nothing), 2 apply. Observe first: it proves the handlers fire
        // and measures the player's own hardware before we add to it.
        const int requested = arg(1, 0);
        out << "move.mode result=" << SetMoveLaneMode(requested) << " mode=" << requested
            << (requested == 0 ? "(off)" : requested == 1 ? "(observe)" : requested == 2 ? "(apply)" : "(unknown)");
    } else if (verb == "move.deadzone") {
        out << "move.deadzone result=" << SetMoveLaneDeadzone(arg(1, 15));
    } else if (verb == "input.post") {
        out << "input.post result=" << SetInputPostEnabled(arg(1, 1));
    } else if (verb == "input.force") {
        out << "input.force result=" << SetInputPostForce(static_cast<unsigned int>(arg(1, 1)));
    } else if (verb == "menu") {
        // Menu navigation through the engine's own input layer. `arg(1)` is the
        // MenuAction index; 0=up 1=down 2=left 3=right 4=accept 5=cancel 6=start.
        // A non-zero result means refused -- it can never mean "the menu did not
        // move", because PostInputEvent returns void and the binding that would
        // consume it is a candidate rather than a proven active bind (R-089).
        const DWORD result = PostMenuAction(static_cast<unsigned int>(arg(1, 0)));
        // `result=0` means **queued**. Delivery happens on the engine thread and
        // acceptance is not observable from here at all: PostInputEvent returns
        // void and the binding is a candidate (R-089). Read the screen.
        out << "menu result=" << result << " action=" << arg(1, 0)
            << " queued=" << InputQueueDepthEstimate()
            << " posted=" << InputPostCount()
            << " refused=" << InputPostRefusedCount()
            << " dropped=" << InputQueueDroppedCount()
            << " drainThread=" << InputDrainThreadId();
    } else if (verb == "input.key") {
        const DWORD result = PostRawInput(arg(1, 0), static_cast<unsigned int>(arg(2, 1)),
                                          arg(3, 1000));
        out << "input.key result=" << result << " keyid=" << arg(1, 0)
            << " queued=" << InputQueueDepthEstimate()
            << " posted=" << InputPostCount()
            << " refused=" << InputPostRefusedCount()
            << " dropped=" << InputQueueDroppedCount()
            << " drainThread=" << InputDrainThreadId();
    } else if (verb == "menu.nav") {
        // Arms the controller -> menu binding. Separate from `input.post` on
        // purpose: one authorises posting at all, the other decides whether a
        // controller may produce it.
        SetMenuNavigation(static_cast<unsigned int>(arg(1, 1)));
        out << "menu.nav result=0 value=" << arg(1, 1)
            << " actions=" << MenuNavigationActionCount();
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
