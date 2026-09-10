#include "preyvr/ConsolePolicy.h"

#include <array>
#include <cctype>
#include <string_view>

namespace preyvr::console {
namespace {

// Kept sorted for readability, not for lookup -- the list is short enough that a
// linear scan is irrelevant and a binary search would just be a chance to get
// the ordering wrong.
// The six weapon-offset entries were added 2026-09-03 on the owner's explicit
// decision, not for convenience. They exist to settle H-005's cheapest route with
// one live test: the fleet graph pointed at FarCry2-vr finding Dunia's weapon
// camera offset -- reachable with no render-pass hook and no engine call -- and
// PreyDll.dll turns out to carry the same family. If they are live, weapon
// placement needs neither a bone API nor a transform-writer hook.
//
// They are the first *writable gameplay* entries here; everything above them is a
// renderer or debug toggle. That is a real widening of what this list permits, so
// it is recorded rather than slipped in. `SetWeaponCameraOffset*` moves the first
// person camera offset and `i_offset_*` are CryEngine's standard item viewmodel
// offsets; both are reversible by setting them back, and neither reaches outside
// the game.
// **Animation control, added 2026-09-05 on an explicit decision.** These are a
// second real widening, and a different kind from the weapon offsets above: they
// do not move something, they *switch a subsystem off*.
//
// The reasoning is that the shipped mod does not want Prey's idle and weapon
// animation at all -- motion controllers own the hands in the end state, so the
// animator is not an obstacle to work around but a thing to disable. Three
// sessions were spent trying to out-race it (R-082, R-083, and a producer chain
// that recomputes at every level) before that was said out loud.
//
// `ca_DebugADIKTargets` earns its place separately: it *draws* the
// animation-driven IK targets, which replaces a day of inferring things from
// single-frame flickers with looking at them.
//
// All four are reversible by setting them back and none reaches outside the game,
// which is the standard the weapon-offset entries were judged against. Expect
// intermediate builds to look broken before they look right: hands with no IK may
// fall to a bind pose or leave the weapon entirely, which is correct-but-ugly
// rather than wrong.
// **The HUD controls, added 2026-09-08.** A third widening, and the narrowest:
// five cvars that change only what the interface draws and where.
//
// The reason they are needed is that the HUD lane cannot be tuned without them.
// Prey's 2D layer renders into a centred 16:9 box fitted inside the frame --
// measured, not assumed: at 3840x1440 the content occupies 60% of the width and
// all of the height, at 2688x2880 it occupies 52% of the height, and 52.5% is
// exactly what a 16:9 box inside that frame gives. So the render aspect places
// the HUD, and these adjust it from there.
//
// `hud_bobHud` is the one that matters most for comfort: a HUD that bobs with
// the walk cycle is attached to the head in VR, and head-locked motion the neck
// did not command is the standard cause of sickness. `hud_hide` and
// `hud_reticleSetting` exist for the case where the mod draws its own symbol and
// the native one must go, which the reticle report named. `g_reticleYPercentage`
// is where the engine's own reset reads the reticle's Y from (R-109).
//
// None reaches outside the game, all are reversible by setting them back, and
// unlike the resolution entries none of them touches the swapchain, so there is
// no latched-size hazard to guard.
constexpr std::array<std::string_view, 42> kAllowlist{
    "g_reticleYPercentage",
    "hud_bobHud",
    "hud_canvas_width_adjustment",
    "hud_hide",
    "hud_reticleSetting",
    "SetWeaponCameraOffsetX",
    "SetWeaponCameraOffsetY",
    "SetWeaponCameraOffsetZ",
    "a_poseAlignerEnable",
    "ca_DebugADIKTargets",
    "ca_NoAnim",
    "ca_useADIKTargets",
    "e_CameraFreeze",
    "e_CoverageBufferDebugFreeze",
    "e_Recursion",
    "e_TimeOfDaySpeed",
    "g_detachCamera",
    "i_offset_front",
    "i_offset_right",
    "i_offset_up",
    "r_AntialiasingMode",
    // --- resolution -------------------------------------------------------
    //
    // Added 2026-09-08, deliberately and not for convenience. The allowlist is
    // fail-closed by design and widening it needs a reason; this is one. The
    // headset receives 48% of the pixels its runtime asks for, with the deficit
    // almost entirely vertical (2560x1440 supplied against 2688x2880 wanted),
    // and these are the native levers the resolution investigation identified.
    // Without them the measurement cannot be acted on at all.
    //
    // Each is a renderer quality/size control. None reaches gameplay, saves, or
    // anything the mod's landmark gate protects.
    "r_CustomResHeight",
    "r_CustomResMaxSize",
    "r_CustomResWidth",
    "r_Height",
    "r_Supersampling",
    "r_SupersamplingFilter",
    "r_Width",
    "r_DrawNearFoV",
    "r_MotionBlur",
    "r_NoDrawNear",
    "r_VSync",
    "sys_MaxFPS",
    "t_Scale",
    // --- native HUD element toggles ---------------------------------------
    //
    // Added 2026-09-11, with a reason, as the policy requires. A wearer asked
    // how to switch off a descriptive box under the inventory and the honest
    // answer was "you cannot, from here" -- the engine has cvars for exactly
    // that, and the only thing in the way was this list.
    //
    // Read from the engine's own registration help strings in the target:
    //   hud_tutorials       "Tutorial mode. 0=Off, 1=Non-Tutorial Prompts
    //                        Only, 2=All"   (registered default 2)
    //   hud_showLegends     "Toggles visual state of input legends."
    //   hud_showOptionalHud "Toggles visual state of all optional hud
    //                        elements."
    //   hud_showHudLog      "Toggles visual state of the pickup and combat
    //                        notification log"
    //
    // Each is a display toggle, reversible by setting it back, and none
    // touches gameplay, saves, the swapchain, or anything the landmark gate
    // protects. They are named individually rather than by a `hud_` prefix
    // rule, because a prefix rule would silently admit every future hud_ cvar.
    "hud_showHudLog",
    "hud_showLegends",
    "hud_showOptionalHud",
    "hud_tutorials",
    // --- the engine's own console UI --------------------------------------
    //
    // Added 2026-09-11 at the wearer's request. Prey has no key bound to the
    // console, and this project's notes concluded from that it "ships no
    // developer console" and routed around it for months. The UI is in fact
    // fully present in the retail binary: CXConsole::Init registers both of
    // these with Crytek's own help text ("Opens the console" / "Closes the
    // console"), and each is five instructions that load the console object
    // and tail-call ShowConsole through vtable slot +0x68.
    //
    // These are COMMANDS, not cvars. Classify only inspects the first token,
    // so a bare verb classifies as a query and executes; the name-based
    // helper below reads "cvar" for historical reasons only.
    //
    // sys_DeactivateConsole is the engine's own gate ("0: normal console
    // behavior / 1: hide the console"), so it has to be settable or opening
    // the console may silently do nothing.
    //
    // Deliberately NOT added: con_restricted. Setting it to 0 lifts the
    // engine's restriction on every other command at once, which is a
    // different decision from three named entries and is the wearer's to make.
    "ConsoleHide",
    "ConsoleShow",
    "sys_DeactivateConsole",
    // **The positive control for the console's missing text.**
    //
    // ConsoleShow works -- it drops the shade, which is the background
    // EngineAssets/Textures/White.dds that CXConsole::Init loads -- but nothing
    // types. CXConsole draws its text through the IFFont it takes in Init as
    // GetFont("default"), and the binary carries the matching failure strings
    // "Error loading the default font from " and "Fonts/default.xml". If that
    // font is not in the retail package, m_pFont is null and exactly this is
    // seen: background yes, glyphs no.
    //
    // The paks are encrypted CryPak (header d6 9a 1a 45, not PK), so the font's
    // presence cannot be settled offline -- searching them returned zero for
    // every needle including the filename table, which is a coverage failure
    // and not a negative result.
    //
    // r_DisplayInfo draws through the same IFFont path, so it separates the two
    // explanations in one command: text on screen means the font is fine and the
    // console's text failure is elsewhere; nothing means the font is the cause.
    // A renderer debug toggle, the same category as the r_ entries above.
    "r_DisplayInfo",
};

bool IsSpace(char c) {
    return c == ' ' || c == '\t';
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && IsSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && IsSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto lowerA = static_cast<char>(
            std::tolower(static_cast<unsigned char>(a[i])));
        const auto lowerB = static_cast<char>(
            std::tolower(static_cast<unsigned char>(b[i])));
        if (lowerA != lowerB) {
            return false;
        }
    }
    return true;
}

// Any character that could separate, quote, or reach the filesystem. Rejecting
// these up front is what makes the allowlist meaningful: without it,
// `t_Scale 0; quit` passes a first-token check.
bool ContainsForbiddenCharacter(std::string_view command) {
    for (const char c : command) {
        switch (c) {
            case '\n':
            case '\r':
            case ';':
            case '"':
            case '\'':
            case '\\':
            case '/':
            case '\0':
                return true;
            default:
                break;
        }
        // Control characters have no business in a console line and are an easy
        // way to hide something from a log.
        if (static_cast<unsigned char>(c) < 0x20) {
            return true;
        }
    }
    return false;
}

// A numeric literal: optional sign, digits, at most one decimal point. No
// exponents, no hex -- scene setup does not need them, and every syntax we
// accept is syntax we have to reason about.
bool IsNumericLiteral(std::string_view text) {
    if (text.empty() || text.size() > 24) {
        return false;
    }
    std::size_t index = 0;
    if (text[index] == '+' || text[index] == '-') {
        ++index;
    }
    bool sawDigit = false;
    bool sawPoint = false;
    for (; index < text.size(); ++index) {
        const char c = text[index];
        if (c == '.') {
            if (sawPoint) {
                return false;
            }
            sawPoint = true;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
        sawDigit = true;
    }
    return sawDigit;
}

} // namespace

bool IsAllowlistedCvar(std::string_view name) {
    for (const std::string_view entry : kAllowlist) {
        // Console variable names are matched case-insensitively by CryEngine, so
        // matching case-sensitively here would let `T_SCALE 0` bypass the
        // allowlist while still executing.
        if (EqualsIgnoreCase(entry, name)) {
            return true;
        }
    }
    return false;
}

Classification Classify(std::string_view command) {
    const std::string_view trimmed = Trim(command);
    if (trimmed.empty() || trimmed.size() > kMaxCommandLength) {
        return Classification::denied;
    }
    if (ContainsForbiddenCharacter(trimmed)) {
        return Classification::denied;
    }

    const std::size_t split = trimmed.find(' ');
    const std::string_view name = trimmed.substr(0, split);

    // Checked explicitly rather than relying on its absence from the allowlist,
    // because `exec` is the one command ExecuteString treats specially: it runs
    // a file from disk and bypasses the deferred-execution queue.
    if (EqualsIgnoreCase(name, "exec")) {
        return Classification::denied;
    }
    if (!IsAllowlistedCvar(name)) {
        return Classification::denied;
    }

    if (split == std::string_view::npos) {
        return Classification::query;
    }

    const std::string_view argument = Trim(trimmed.substr(split + 1));
    if (argument.empty()) {
        // Unreachable while the whole line is trimmed up front: `trimmed` cannot
        // end in whitespace, so something non-blank always follows the first
        // space. Kept so the parse stays total if that ever changes.
        return Classification::denied;
    }
    if (argument.find(' ') != std::string_view::npos) {
        return Classification::denied; // more than one argument
    }
    if (!IsNumericLiteral(argument)) {
        return Classification::denied;
    }
    return Classification::sceneControl;
}

} // namespace preyvr::console
