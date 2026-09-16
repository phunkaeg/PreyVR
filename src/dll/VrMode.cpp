#include "VrMode.h"
#include "Logger.h"
#include "XrSessionHost.h"
#include "XrInput.h"
#include "CameraEditHook.h"
#include "FrameObserverHook.h"
#include "ConsoleBridgeWin32.h"
#include "HeadTrackingHook.h"
#include "NearViewStereo.h"
#include "NearFovOverride.h"
#include "InputPost.h"
#include "InputPathProbe.h"
#include "MoveLane.h"
#include "AimTakeover.h"
#include "ReticleFollow.h"
#include "HudLayer.h"
#include "UiPointer.h"
#include "AnimIkTakeover.h"
#include "preyvr/LatestSnapshot.h"
#include <cmath>
#include <sstream>

namespace preyvr::dll {
namespace {
enum class Phase { off, preparing, settings, tracking, rendering, active, failed };
Phase phase=Phase::off;
std::string failure;
unsigned setting=0;
unsigned long long deadline=0, consoleCount=0, calibratedOwner=0, calibratedReference=0;
bool consolePending=false;
unsigned desiredWidth=0,desiredHeight=0;
unsigned long long firstFrame=0;
unsigned long long nextInputProbe=0;
const char* settings[]={"r_MotionBlur 0","r_AntialiasingMode 1"};
void Log(const std::string& s) { lifecycle::Log("preyvr_vr " + s); }
void StopFeatures() {
    SetHudLayerEnabled(0);
    SetSyntheticStereo(0,50);
    SetViewHookApplying(0); SetViewPositionApplying(0);
    SetNearViewStereo(0); SetNearFovDeciDegrees(0);
    SetReticleFollowEnabled(0); SetAimTakeoverEnabled(0);
    SetAnimIkControllerDrive(0); SetAnimIkMode(0);
    SetMenuNavigation(0); SetMoveLaneMode(0);
    SetTurnLaneEnabled(0); SetFireLaneEnabled(0); SetInteractionEnabled(0);
    // Leave the input drain alive to deliver the owned releases on the next frame.
    StopXrSession();
}
void Fail(const char* step) {
    failure=step; phase=Phase::failed; StopFeatures();
    Log("state=failed step="+failure+" retry=F11");
}
}
void EnableVrMode() {
    if(phase!=Phase::off && phase!=Phase::failed) return;
    failure.clear(); calibratedOwner=0;
    if(SetFrameObserverEnabled(true)!=2 || EnsureRenderHookInstalled()!=0) {
        Fail("render_hooks"); return;
    }
    SetNativeProjection(1); // returns CameraEditStatus, not a zero-success result.
    if(!NativeProjectionEnabled() || SetViewHookObserving(1)!=0) {
        Fail("view_hook"); return;
    }
    wchar_t value[24]{};
    desiredWidth=GetEnvironmentVariableW(L"PREYVR_RENDER_WIDTH",value,24)?static_cast<unsigned>(_wtoi(value)):0;
    desiredHeight=GetEnvironmentVariableW(L"PREYVR_RENDER_HEIGHT",value,24)?static_cast<unsigned>(_wtoi(value)):0;
    const auto curveLength=GetEnvironmentVariableW(L"PREYVR_UI_CURVE_DEGREES",value,24);
    if(curveLength>0 && curveLength<24)SetUiCurveDegrees(static_cast<unsigned>(_wtoi(value)));
    const auto pointerLength=GetEnvironmentVariableW(L"PREYVR_POINTER_HAND",value,24);
    if(pointerLength>0 && pointerLength<24)SetUiPointerHand(static_cast<unsigned>(_wtoi(value)));
    phase=Phase::preparing;deadline=GetTickCount64()+120000;nextInputProbe=0;
    Log("state=waiting_for_renderer controls=F11_enable,F12_recenter");
}
void DisableVrMode() {
    StopFeatures(); phase=Phase::off; Log("state=off");
}
void TickVrMode() {
    // Poll the current process's foreground window only. Refocus with a key held
    // must not trigger activation or recenter; an actual new press is required.
    static bool focusWas=false, f11Was=false, f12Was=false;
    DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    const bool focus=pid==GetCurrentProcessId();
    const bool f11=(GetAsyncKeyState(VK_F11)&0x8000)!=0;
    const bool f12=(GetAsyncKeyState(VK_F12)&0x8000)!=0;
    if(focus && focusWas) {
        if(f11 && !f11Was) EnableVrMode();
        if(f12 && !f12Was) RecenterHeadTracking();
    }
    focusWas=focus; f11Was=f11; f12Was=f12;
    if(XrSessionLossPending()) {Fail("openxr_session_ended");return;}
    if(phase==Phase::preparing) {
        if(GetTickCount64()>deadline) { Fail("render_size_timeout"); return; }
        if(!XrBackbufferReady(desiredWidth,desiredHeight))return;
        // A native window and renderer can exist before gEnv.pInput. In the
        // cold-start control this was still null at injection, then resolved on
        // retry. Wait for that explicit not-ready result; reject a bad contract.
        if(GetTickCount64()<nextInputProbe)return;
        nextInputProbe=GetTickCount64()+250;
        const auto inputPath=ResolveInputPath();
        if(inputPath==2)return;
        if(inputPath!=0 || SetInputPostEnabled(1)!=0){Fail("input_contract");return;}
        firstFrame=XrSubmittedFrameCount();
        if(XrSessionStatusValue()!=1) {
            SetXrPreferSrgbFormat(1);
            if(StartXrSession()!=1) { Fail("openxr_start"); return; }
        }
        if(!XrInputCreated()) { Fail("openxr_actions"); return; }
        SetUiPanelMode(1);SetMenuNavigationGate(1);SetMenuNavigation(1);
        phase=Phase::settings;setting=0;consolePending=false;deadline=GetTickCount64()+15000;
        Log("state=starting renderer=settled");
    }
    if(phase==Phase::settings) {
        if(GetTickCount64()>deadline) { Fail("renderer_settings_timeout"); return; }
        if(consolePending) {
            if(SubmittedConsoleCommandCount()==consoleCount) return;
            if(LastConsoleBridgeResult()!=0) { Fail("renderer_settings"); return; }
            consolePending=false; ++setting;
        }
        if(setting<std::size(settings)) {
            consoleCount=SubmittedConsoleCommandCount();
            const auto result=QueueConsoleCommand(settings[setting]);
            if(result==4) return;
            if(result!=0) { Fail("queue_renderer_settings"); return; }
            consolePending=true; return;
        }
        phase=Phase::tracking; deadline=GetTickCount64()+120000;
        Log("state=waiting_for_headset put_on_headset=1");
    }
    if(phase==Phase::tracking) {
        if(GetTickCount64()>deadline) { Fail("headset_tracking_timeout"); return; }
        TrackingFrame frame{};
        if(!TryGetTrackingFrame(frame) || !FreshSample(MonotonicNanoseconds(),frame.publishedNs) ||
            !IsPoseUsable(frame.head,frame.headValidity,200000000ull)) return;
        const float ipd=XrRuntimeIpdMetres();
        if(!std::isfinite(ipd) || ipd<.045f || ipd>.085f) { Fail("runtime_eye_separation"); return; }
        const auto width=XrResolutionChain(4),height=XrResolutionChain(5);
        if(!width || !height) { Fail("render_dimensions"); return; }
        const auto nearFov=static_cast<unsigned>(std::lround(
            2*std::atan(std::tan(1.0471975512f)*static_cast<float>(height)/width)*572.9577951f));
        if(RecenterHeadTracking()!=0) return;
        if(SetViewHookApplying(1)!=0 || SetViewPositionApplying(1)!=0 ||
            SetSyntheticStereo(ipd,50)!=2 || SetXrStereoSubmission(1)!=1 ||
            SetNearViewStereo(1)!=0 || SetNearViewIpdMetres(ipd)!=0 || SetNearFovDeciDegrees(nearFov)!=0) {
            Fail("stereo_and_tracking"); return;
        }
        if(SetMoveLaneMode(2)!=0 || SetTurnLaneEnabled(1)!=0 || SetFireLaneEnabled(1)!=0 ||
            SetInteractionEnabled(1)!=0 || SetAimTakeoverEnabled(1)!=0 ||
            SetReticleFollowEnabled(1)!=0 || SetReticleDispatchEnabled(1)!=0 ||
            SetAnimIkMode(2)!=0 || SetAnimIkControllerDrive(1)!=0 ||
            SetAnimIkWeaponAlignment(1)!=0 || SetAnimIkHands(3)!=0 || SetAnimIkReachPercent(65)!=0) {
            Fail("motion_controls"); return;
        }
        wchar_t hudSetting[8]{};
        GetEnvironmentVariableW(L"PREYVR_HUD_LAYER",hudSetting,8);
        if(hudSetting[0]!=L'0' && SetHudLayerEnabled(1)!=0)Log("hud_layer=unavailable native_hud=retained");
        phase=Phase::rendering;deadline=GetTickCount64()+15000;
    }
    if(phase==Phase::rendering) {
        if(GetTickCount64()>deadline) { Fail("no_rendered_frames"); return; }
        if(XrSubmittedFrameCount()<firstFrame+3 || XrResolutionChain(14)!=0)return;
        phase=Phase::active;
        Log("state=active panel=automatic recenter=F12_or_grips_and_left_Y");
    }
    if(phase==Phase::active) {
        if(XrSessionStatusValue()!=1) {Fail("openxr_session_ended");return;}
        const auto owner=AnimIkOwnerGeneration();
        const auto reference=HeadTrackingReferenceGeneration();
        if(AnimIkOwnerCharacter()!=0 && reference%2==0 &&
           (owner!=calibratedOwner || reference!=calibratedReference) && CalibrateAnimIk()==0) {
            calibratedOwner=owner;
            calibratedReference=reference;
        }
    }
}
std::string VrModeReport() {
    const char* names[]={"off","preparing","settings","tracking","rendering","active","failed"};
    std::ostringstream s;
    s<<"vr="<<names[static_cast<int>(phase)]<<" vrFailure="<<(failure.empty()?"none":failure)
     <<" uiPanel="<<UiPanelMode()<<" uiPanelFrames="<<UiPanelFrameCount();
    return s.str();
}
}
