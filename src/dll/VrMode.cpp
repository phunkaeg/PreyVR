#include "VrMode.h"
#include "VrOptionsRuntime.h"
#include "Logger.h"
#include "XrSessionHost.h"
#include "XrInput.h"
#include "CameraEditHook.h"
#include "FrameObserverHook.h"
#include "ConsoleBridgeWin32.h"
#include "HeadTrackingHook.h"
#include "NearViewStereo.h"
#include "NearParticleStereo.h"
#include "DualSceneStereo.h"
#include "DualOverlay.h"
#include "NearFovOverride.h"
#include "InputPost.h"
#include "preyvr/InputEvent.h"
#include "InputPathProbe.h"
#include "MoveLane.h"
#include "HudBridge.h"
#include "AimTakeover.h"
#include "ReticleFollow.h"
#include "HudLayer.h"
#include "UiPointer.h"
#include "AnimIkTakeover.h"
#include "InteractionLane.h"
#include "CarryLane.h"
#include "GrenadeLane.h"
#include "DebugOverlay.h"
#include "BodyFollow.h"
#include "preyvr/DebugOverlayScene.h"
#include "preyvr/LatestSnapshot.h"
#include "MinHookInit.h"
#include <MinHook.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace preyvr::dll {
namespace {
enum class Phase { off, preparing, settings, tracking, rendering, active, failed };
Phase phase=Phase::off;
std::string failure;
unsigned setting=0;
unsigned long long deadline=0, consoleCount=0, calibratedOwner=0, calibratedReference=0;
// The frame rate, stick, both-eyes and HUD-panel counters, logged every 30 s while VR is active
// so a headset session records them without a tuner command.
unsigned long long nextStatusReport=0;
constexpr unsigned long long kStatusReportMs=30000;
bool consolePending=false;
unsigned desiredWidth=0,desiredHeight=0;
unsigned long long firstFrame=0;
bool nearFollow=true;
unsigned followedNearFov=0;
unsigned long long nextInputProbe=0;
// r_VSync 0: the headset's xrWaitFrame paces the game. Vsync to the PC's monitor
// is a second clock against it -- every VR mod precedent turns it off.
const char* settings[]={"r_MotionBlur 0","r_AntialiasingMode 1","r_VSync 0"};
void Log(const std::string& s) { lifecycle::Log("preyvr_vr " + s); }
// The 3D VIEW option (VrMode.h). Owned by the command worker, like the rest of this file's state.
// Alternate eyes by default: both eyes per frame costs twice the GPU and held ~19 fps
// on the RTX 3060 laptop (headset, 2026-10-17); at the headset's rate one eye per
// frame is the precedent that plays (Luke Ross R.E.A.L., UEVR's AFR).
unsigned stereoChosen=1, stereoActive=1;
// Until the player picks a 3D VIEW while playing, alternate eyes may fall back to
// mono when they cannot hold the headset's rate (see TickVrMode).
bool stereoAuto=true;
unsigned long long rateWindowStart=0, rateWindowFrames=0;
unsigned slowRateWindows=0;
// PREYVR_FAKE_FOCUS=1 (the mock harness only): the game is told its window has
// focus. With the desktop locked it never does, so it pauses itself on loading
// and drops the pause menu's input; these three are what PreyDll asks.
HWND gOwnWindow=nullptr;
using WindowQueryFn=HWND(WINAPI*)();
WindowQueryFn gForeground=nullptr,gActive=nullptr,gFocus=nullptr;
BOOL CALLBACK FindOwnWindow(HWND h,LPARAM) {
    DWORD pid=0;GetWindowThreadProcessId(h,&pid);
    if(pid!=GetCurrentProcessId()||GetWindow(h,GW_OWNER)||!IsWindowVisible(h))return TRUE;
    gOwnWindow=h;return FALSE;
}
HWND OwnWindow(WindowQueryFn original) {
    if(!gOwnWindow)EnumWindows(&FindOwnWindow,0);
    return gOwnWindow?gOwnWindow:original();
}
HWND WINAPI ForegroundShim(){return OwnWindow(gForeground);}
HWND WINAPI ActiveShim(){return OwnWindow(gActive);}
HWND WINAPI FocusShim(){return OwnWindow(gFocus);}
void InstallFocusShim() {
    wchar_t value[8]{};
    if(!GetEnvironmentVariableW(L"PREYVR_FAKE_FOCUS",value,8)||value[0]!=L'1'||!EnsureMinHook())return;
    const auto hook=[](const char* name,LPVOID detour,WindowQueryFn* original) {
        LPVOID target=nullptr;
        return MH_CreateHookApiEx(L"user32",name,detour,reinterpret_cast<LPVOID*>(original),&target)==MH_OK&&
               MH_EnableHook(target)==MH_OK;
    };
    const bool ok=hook("GetForegroundWindow",reinterpret_cast<LPVOID>(&ForegroundShim),&gForeground)&&
                  hook("GetActiveWindow",reinterpret_cast<LPVOID>(&ActiveShim),&gActive)&&
                  hook("GetFocus",reinterpret_cast<LPVOID>(&FocusShim),&gFocus);
    Log(std::string("fake_focus=")+(ok?"on":"failed"));
}

bool stereoApplied=false;
// Switches the three paths in the order that never publishes a both-eyes contract without its second submission:
// the render-thread mode is armed before the game thread's contract, and the contract is dropped before the mode.
void ApplyStereoMode() {
    const char* names[]={"mono","alternate","full"};
    unsigned active=stereoChosen;
    if(stereoChosen==2) {
        SetXrMonoView(0);
        if(SetDualSceneMode(3,0)==0) {
            SetDualStereoContract(1);
            // What the game draws after the world, into the held eye too (DualOverlay.h).
            // PREYVR_DUAL_OVERLAY=0 leaves it in one eye.
            wchar_t overlaySetting[8]{};
            GetEnvironmentVariableW(L"PREYVR_DUAL_OVERLAY",overlaySetting,8);
            if(overlaySetting[0]==L'0') Log("dual_overlay=off reason=PREYVR_DUAL_OVERLAY");
            else if(SetDualOverlayRedraw(1)!=0) Log("dual_overlay=unavailable overlays=second_eye_only");
            // The launcher reports the mode from these lines (Get-PreyVRStereoStatus).
            Log("dual_stereo=on");
        } else {
            active=1;
            Log("dual_stereo=unavailable fallback=alternate_eye");
        }
    }
    if(active!=2) {
        SetDualStereoContract(0); SetDualSceneMode(0,0); SetDualOverlayRedraw(0);
        SetXrMonoView(active==0 ? 1u : 0u);
        if(stereoChosen==0) Log("dual_stereo=mono reason=3D_VIEW");
        else if(stereoChosen==1) Log("dual_stereo=off reason=3D_VIEW_or_PREYVR_DUAL_STEREO");
    }
    stereoActive=active; stereoApplied=true;
    Log(std::string("stereo_mode=")+names[active]+" chosen="+names[stereoChosen]);
}
void StopFeatures() {
    SetHudLayerEnabled(0);
    // Contract first, so no both-eyes record is published after its submissions stop.
    SetDualStereoContract(0); SetDualSceneMode(0,0); SetDualOverlayRedraw(0);
    SetXrMonoView(0); stereoApplied=false;
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
// A failure the runtime caused (no headset yet, a lost or replaced session) is
// retried on its own after retryMs; only the user's EXITING and the mod's own
// faults wait for F11. Nothing a headset does should need a key on the PC.
unsigned long long retryAt=0;
bool paused=false;
void Fail(const char* step, unsigned long long retryMs=0) {
    failure=step; phase=Phase::failed; StopFeatures(); paused=false;
    retryAt=retryMs?GetTickCount64()+retryMs:0;
    Log("state=failed step="+failure+(retryMs?" retry=auto_ms_"+std::to_string(retryMs):" retry=F11"));
}
}
void EnableVrMode() {
    if(phase!=Phase::off && phase!=Phase::failed) return;
    failure.clear(); calibratedOwner=0; retryAt=0;
    if(SetFrameObserverEnabled(true)!=2 || EnsureRenderHookInstalled()!=0) {
        Fail("render_hooks"); return;
    }
    SetNativeProjection(1); // returns CameraEditStatus, not a zero-success result.
    if(!NativeProjectionEnabled() || SetViewHookObserving(1)!=0) {
        Fail("view_hook"); return;
    }
    InstallFocusShim();
    wchar_t value[24]{};
    desiredWidth=GetEnvironmentVariableW(L"PREYVR_RENDER_WIDTH",value,24)?static_cast<unsigned>(_wtoi(value)):0;
    desiredHeight=GetEnvironmentVariableW(L"PREYVR_RENDER_HEIGHT",value,24)?static_cast<unsigned>(_wtoi(value)):0;
    const auto curveLength=GetEnvironmentVariableW(L"PREYVR_UI_CURVE_DEGREES",value,24);
    if(curveLength>0 && curveLength<24)SetUiCurveDegrees(static_cast<unsigned>(_wtoi(value)));
    const auto pointerLength=GetEnvironmentVariableW(L"PREYVR_POINTER_HAND",value,24);
    if(pointerLength>0 && pointerLength<24)SetUiPointerHand(static_cast<unsigned>(_wtoi(value)));
    LoadVrOptions();
    phase=Phase::preparing;deadline=GetTickCount64()+120000;nextInputProbe=0;
    Log("state=waiting_for_renderer controls=F11_enable,F12_recenter");
}
void DisableVrMode() {
    StopFeatures(); phase=Phase::off; Log("state=off");
}
void TickVrMode() {
    // Poll the current process's foreground window only. Refocus with a key held
    // must not trigger activation or recenter; an actual new press is required.
    static bool focusWas=false, f10Was=false, f11Was=false, f12Was=false;
    DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    const bool focus=pid==GetCurrentProcessId();
    const bool f11=(GetAsyncKeyState(VK_F11)&0x8000)!=0;
    const bool f12=(GetAsyncKeyState(VK_F12)&0x8000)!=0;
    const bool f10=(GetAsyncKeyState(VK_F10)&0x8000)!=0;
    if(focus && focusWas) {
        if(f10 && !f10Was)RequestVrOptions(!VrOptionsOpen());
        if(f11 && !f11Was) EnableVrMode();
        if(f12 && !f12Was) RecenterHeadTracking();
    }
    focusWas=focus; f11Was=f11; f12Was=f12;
    f10Was=f10;
    if(XrSessionLossPending()) {
        // EXITING is the user leaving VR from the runtime's menu: respected. A loss is not theirs.
        if(XrSessionExiting()) Fail("openxr_session_exited");
        else Fail("openxr_session_lost",5000);
        return;
    }
    if(XrSessionKickDue()) {Fail("openxr_kick",1);return;}
    // The old session is torn down on the render thread; a new one starts only after.
    if(phase==Phase::failed && retryAt && GetTickCount64()>=retryAt && XrSessionStatusValue()!=1) {
        Log("state=retrying step="+failure);
        EnableVrMode();
    }
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
            // No headset or no Link yet: try again until there is one.
            if(StartXrSession()!=1) { Fail("openxr_start",5000); return; }
        }
        if(!XrInputCreated()) { Fail("openxr_actions"); return; }
        SetUiPanelMode(1);SetMenuNavigationGate(1);SetMenuNavigation(1);
        phase=Phase::settings;setting=0;consolePending=false;deadline=GetTickCount64()+15000;
        Log("state=starting renderer=settled");
    }
    if(phase==Phase::settings) {
        if(GetTickCount64()>deadline) { Fail("renderer_settings_timeout"); return; }
        if(consolePending) {
            if(VerifiedRendererSettingCount()==consoleCount) return;
            if(VerifiedRendererSettingResult()!=0) { Fail("renderer_settings"); return; }
            consolePending=false; ++setting;
        }
        if(setting<std::size(settings)) {
            consoleCount=VerifiedRendererSettingCount();
            const auto result=QueueVerifiedRendererSetting(settings[setting]);
            if(result==4) return;
            if(result!=0) { Fail("queue_renderer_settings"); return; }
            consolePending=true; return;
        }
        phase=Phase::tracking; deadline=GetTickCount64()+120000;
        Log("state=waiting_for_headset put_on_headset=1");
    }
    if(phase==Phase::tracking) {
        if(GetTickCount64()>deadline) { Fail("headset_tracking_timeout",1000); return; }
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
        // The 3D VIEW option (both eyes every frame, alternate eyes, or mono). PREYVR_DUAL_STEREO=0 -- the
        // launcher's DualStereo 0 -- still forces the alternate-eye path for this launch.
        {
            wchar_t dualSetting[8]{};
            GetEnvironmentVariableW(L"PREYVR_DUAL_STEREO",dualSetting,8);
            if(dualSetting[0]==L'0' && stereoChosen==2) {
                stereoChosen=1;
                Log("stereo_mode=alternate reason=PREYVR_DUAL_STEREO");
            }
            ApplyStereoMode();
        }
        // DrawNear particles in the weapon's camera space (preyvr/NearParticles.h).
        // Not fatal: without the hook they keep the native double parallax.
        EnsureNearParticleStereo();
        if(SetMoveLaneMode(2)!=0 || SetTurnLaneEnabled(1)!=0 || SetFireLaneEnabled(1)!=0 ||
            SetInteractionEnabled(1)!=0 || SetAimTakeoverEnabled(1)!=0 ||
            SetReticleFollowEnabled(1)!=0 || SetReticleDispatchEnabled(1)!=0 ||
            SetAnimIkMode(2)!=0 || SetAnimIkControllerDrive(1)!=0 ||
            SetAnimIkWeaponAlignment(1)!=0 || SetAnimIkHands(3)!=0 || SetAnimIkReachPercent(65)!=0) {
            Fail("motion_controls"); return;
        }
        // World interaction from the LEFT hand: it points, its grip uses; the
        // right grip only reloads (InteractionLane.h). Not fatal: without the
        // selector hook the right grip stays the native use/reload button.
        if(SetUseHand(1)!=0) Log("use_hand=right reason=selector_unavailable");
        // Phase 2: what the left hand carries is IN it, held while the grip is,
        // thrown with the hand's motion (CarryLane.h). Not fatal: without the
        // hooks the game's own carry stays, toggled like in phase 1.
        else if(SetCarryHand(1)!=0) Log("carry_hand=native reason=hooks_unavailable");
        // Grenades leave the right hand at the trigger's release with its
        // motion (GrenadeLane.h). Not fatal: without the hooks the game's own
        // charged throw along the view stays.
        if(SetGrenadeHand(1)!=0) Log("grenade_hand=native reason=hooks_unavailable");
        // The arms stay at the controllers with no weapon drawn: holstered (the
        // right grip held) or carrying (AnimIkTakeover arms.free; arms.free 0 =
        // the game lowers them out of view, as before).
        SetAnimIkFreeArms(1);
        // The character turns with the head and walks under it (BodyFollow.h; the
        // VR option chooses how much). Not fatal: without the hook the body keeps
        // the game's own facing and place, as before.
        if(EnsureBodyFollowHook()!=0) Log("body_follow=unavailable");
        // Both hands' rays drawn in gameplay while interaction is being tested
        // (user, 2026-10-06). The VR options' Debug overlay switch turns them off.
        if(DebugOverlayMask()==0) SetDebugOverlay(debugdraw::kRays|debugdraw::kHits);
        wchar_t hudSetting[8]{};
        GetEnvironmentVariableW(L"PREYVR_HUD_LAYER",hudSetting,8);
        if(hudSetting[0]!=L'0' && SetHudLayerEnabled(1)!=0)Log("hud_layer=unavailable native_hud=retained");
        phase=Phase::rendering;deadline=GetTickCount64()+15000;
    }
    if(phase==Phase::rendering) {
        if(GetTickCount64()>deadline) { Fail("no_rendered_frames"); return; }
        if(XrSubmittedFrameCount()<firstFrame+3 || XrResolutionChain(14)!=0)return;
        phase=Phase::active;
        nextStatusReport=GetTickCount64()+kStatusReportMs;
        Log("state=active panel=automatic recenter=F12_or_grips_and_left_Y");
    }
    if(phase==Phase::active) {
        if(XrSessionStatusValue()!=1) {Fail("openxr_session_ended",5000);return;}
        // The runtime stopped the session (the headset came off) and will offer it
        // back: everything stays armed, so this is only a note for the log.
        if(const bool running=XrSessionBegun(); running==paused) {
            paused=!running;
            Log(paused?"state=paused reason=runtime_stopped_session":"state=resumed");
            // And Prey is paused with it, as VR games do when the headset comes off:
            // the world must not go on without the player. Opened once, only from
            // play (never over a menu), and left for the player to close.
            // PREYVR_PAUSE_ON_HEADSET_OFF=0 keeps the game running.
            static const bool pauseGame=[]{wchar_t v[4]{};return !GetEnvironmentVariableW(L"PREYVR_PAUSE_ON_HEADSET_OFF",v,4)||v[0]!=L'0';}();
            if(paused && pauseGame && HudGameplayInputAllowed()) {
                const auto posted=PostMenuAction(static_cast<unsigned>(input::MenuAction::Start),0);
                Log("game_pause result="+std::to_string(posted)+" reason=headset_off");
            }
        }
        if(GetTickCount64()>=nextStatusReport) {
            nextStatusReport=GetTickCount64()+kStatusReportMs;
            // The same text dual.report and hud.layer answer with, plus what a headset
            // session needs read back: frames really submitted and the stick's fate.
            static unsigned long long lastSubmitted=0;
            const auto submitted=XrSubmittedFrameCount();
            const auto fps=lastSubmitted?(submitted-lastSubmitted)/(kStatusReportMs/1000):0;
            lastSubmitted=submitted;
            Log("status fps="+std::to_string(fps)+" move={posted="+std::to_string(MoveLanePosted())+
                " dropped="+std::to_string(MoveLaneDropped())+" held="+std::to_string(MoveLaneHeld())+
                " menuSamples="+std::to_string(HudMenuStateSampleCount())+"} dual={"+DualSceneReport()+
                DualOverlayReport()+" } hud={"+HudLayerReport()+" }");
        }
        // **Alternate eyes only work at the headset's rate.** Below it each eye
        // updates at half of a low rate and Meta's spacewarp mixes the two: the
        // sister mod measured it much worse than mono at 40 fps. So when they hold
        // under 85% of the rate through three 5 s windows of play, this launch
        // falls back to mono, which is comfortable at any rate. Menus and loads
        // (slow by nature) and a paused session do not count.
        {
            const auto now=GetTickCount64();
            const auto period=XrRuntimePeriodUs();
            const bool measuring=stereoAuto && stereoActive==1 && !paused && period && HudGameplayInputAllowed();
            if(!measuring) { rateWindowStart=0; slowRateWindows=0; }
            else if(!rateWindowStart) { rateWindowStart=now; rateWindowFrames=XrSubmittedFrameCount(); }
            else if(now-rateWindowStart>=5000) {
                const double fps=(XrSubmittedFrameCount()-rateWindowFrames)*1000.0/(now-rateWindowStart);
                const double rate=1e6/period;
                slowRateWindows=fps<rate*.85?slowRateWindows+1:0;
                rateWindowStart=now; rateWindowFrames=XrSubmittedFrameCount();
                if(slowRateWindows>=3) {
                    Log("stereo_auto=mono reason=alternate_below_rate fps="+std::to_string(static_cast<int>(fps))+
                        " rate="+std::to_string(static_cast<int>(rate+.5)));
                    stereoChosen=0; stereoAuto=false; ApplyStereoMode();
                }
            }
        }
        const auto owner=AnimIkOwnerGeneration();
        const auto reference=HeadTrackingReferenceGeneration();
        if(AnimIkOwnerCharacter()!=0 && reference%2==0 &&
           (owner!=calibratedOwner || reference!=calibratedReference) && CalibrateAnimIk()==0) {
            calibratedOwner=owner;
            calibratedReference=reference;
        }
        FollowNearFov();
    }
}
// **The near pass must project exactly like the world.** The arms and the held
// weapon are drawn by the near (viewmodel) pass at r_DrawNearFoV, the world at
// the camera's own frustum, and the image is declared to the runtime with the
// WORLD's tangents. Since the IK places the hands in world space, any other near
// FOV draws them at the wrong angle from the eye: the activation value assumed
// a 120-degree horizontal FOV slider, and with the slider at 98 (cl_hfov 98,
// measured 2026-10-06) the hands were drawn at 0.66 of their angle, pulled
// towards the centre -- short arms, a weapon above the real hand. The near
// FOV now follows the vertical FOV the world camera actually rendered.
void FollowNearFov() {
    if(!nearFollow)return;
    float left=0,right=0,up=0,down=0;
    if(!RenderedEyeTangents(left,right,up,down))return;
    const float vertical=std::max(std::fabs(up),std::fabs(down));
    if(!std::isfinite(vertical) || vertical<.2f || vertical>10.f)return;
    const auto deci=static_cast<unsigned>(std::lround(2*std::atan(vertical)*572.9577951f));
    // Against the value in force, not the last one followed: activation and a
    // VR restart set their own, which must be replaced again.
    if(deci!=NearFovDeciDegrees() && SetNearFovDeciDegrees(deci)==0) {
        if(deci!=followedNearFov)Log("near_fov=follow deci_degrees="+std::to_string(deci));
        followedNearFov=deci;
    }
}
DWORD SetNearFovFollow(unsigned enabled) {
    if(enabled>1)return 1;
    nearFollow=enabled!=0; followedNearFov=0;
    Log(std::string("near_fov_follow=")+(nearFollow?"1":"0"));
    return 0;
}
DWORD SetVrStereoMode(unsigned mode) {
    if(mode>2)return 1;
    stereoChosen=mode;
    // Before activation the choice waits for it; while VR runs it applies now,
    // and from then on it is the player's: no automatic fallback overrides it.
    if(phase==Phase::active){stereoAuto=false;ApplyStereoMode();}
    return 0;
}
unsigned VrStereoModeChosen() { return stereoChosen; }
unsigned VrStereoModeActive() { return stereoApplied ? stereoActive : stereoChosen; }
unsigned NearFovFollowed() { return nearFollow?followedNearFov:0; }
std::string VrModeReport() {
    const char* names[]={"off","preparing","settings","tracking","rendering","active","failed"};
    std::ostringstream s;
    s<<"vr="<<names[static_cast<int>(phase)]<<" vrFailure="<<(failure.empty()?"none":failure)
     <<" uiPanel="<<UiPanelMode()<<" uiPanelFrames="<<UiPanelFrameCount();
    return s.str();
}
}
