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
constexpr std::array<std::string_view, 29> kAllowlist{
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
