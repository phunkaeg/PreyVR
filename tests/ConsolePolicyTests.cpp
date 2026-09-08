#include "preyvr/ConsolePolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using preyvr::console::Classification;
using preyvr::console::Classify;
using preyvr::console::IsAllowlistedCvar;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void RequireDenied(std::string_view command, const char* message)
{
    Require(Classify(command) == Classification::denied, message);
}

void TestAllowedFormsClassify()
{
    Require(Classify("t_Scale 0") == Classification::sceneControl,
        "freezing time is scene control");
    Require(Classify("t_Scale") == Classification::query,
        "a bare allowlisted name is a query");
    Require(Classify("  t_Scale 0  ") == Classification::sceneControl,
        "surrounding whitespace is tolerated");
    // The whole line is trimmed before parsing, so a dangling space carries no
    // meaning and this really is the same input as a bare name.
    Require(Classify("t_Scale ") == Classification::query,
        "a trailing space collapses to a query rather than becoming malformed");
    Require(Classify("r_AntialiasingMode 0") == Classification::sceneControl,
        "disabling temporal AA is scene control");
    Require(Classify("t_Scale 1.5") == Classification::sceneControl,
        "a decimal argument is accepted");
    Require(Classify("t_Scale -1") == Classification::sceneControl,
        "a negative argument is accepted");
    Require(Classify("r_DrawNearFoV 88") == Classification::sceneControl,
        "matching the viewmodel FOV to the world is scene control");
}

void TestCaseInsensitivity()
{
    // CryEngine matches cvar names case-insensitively, so a case-sensitive
    // allowlist would deny nothing -- the command would still execute.
    Require(Classify("T_SCALE 0") == Classification::sceneControl,
        "an uppercased allowlisted name is still allowlisted");
    Require(IsAllowlistedCvar("t_scale"), "lookup ignores case");
    Require(!IsAllowlistedCvar("t_Scale2"), "lookup is not a prefix match");
    Require(!IsAllowlistedCvar("_Scale"), "lookup is not a suffix match");
}

void TestSeparatorsCannotSmuggleCommands()
{
    // The whole point of the allowlist. Without a separator ban, every one of
    // these passes a first-token check and then runs something unapproved.
    RequireDenied("t_Scale 0; quit", "a semicolon-chained command is denied");
    RequireDenied("t_Scale 0\nquit", "a newline-chained command is denied");
    RequireDenied("t_Scale 0\rquit", "a carriage-return-chained command is denied");
    RequireDenied(std::string("t_Scale 0\0quit", 14), "an embedded NUL is denied");
}

void TestExecIsDeniedExplicitly()
{
    // ExecuteString special-cases `exec`: it runs a file from disk and bypasses
    // the deferred-execution queue, so it is denied by name rather than only by
    // absence from the allowlist.
    RequireDenied("exec autoexec.cfg", "exec is denied");
    RequireDenied("EXEC autoexec.cfg", "exec is denied regardless of case");
    RequireDenied("exec", "a bare exec is denied");
}

void TestUnknownCommandsAreDenied()
{
    RequireDenied("quit", "an unallowlisted command is denied");
    RequireDenied("map neuromod_division", "map loading is not allowlisted");
    RequireDenied("g_godMode 1", "an unallowlisted cvar is denied even though it exists");
    RequireDenied("save_game", "anything touching persistence is denied");
}

void TestMalformedInputIsDenied()
{
    RequireDenied("", "an empty command is denied");
    RequireDenied("   ", "a whitespace-only command is denied");
    RequireDenied("t_Scale 0 1", "more than one argument is denied");
    RequireDenied("t_Scale abc", "a non-numeric argument is denied");
    RequireDenied("t_Scale 1.2.3", "a malformed number is denied");
    RequireDenied("t_Scale .", "a lone decimal point is denied");
    RequireDenied("t_Scale -", "a lone sign is denied");
    RequireDenied("t_Scale 0x10", "hexadecimal is denied");
    RequireDenied("t_Scale 1e5", "exponent notation is denied");
    RequireDenied(std::string("t_Scale ") + std::string(200, '0'), "an overlong command is denied");
}

void TestPathAndQuoteCharactersAreDenied()
{
    RequireDenied("t_Scale ../evil", "a relative path is denied");
    RequireDenied("t_Scale \"0\"", "a quoted argument is denied");
    RequireDenied("t_Scale C:\\x", "a backslash path is denied");
}

void TestAllowlistContents()
{
    // Each of these is on the list for a stated reason; if one is removed the
    // reason should go with it.
    Require(IsAllowlistedCvar("t_Scale"), "time scale is allowlisted");
    Require(IsAllowlistedCvar("r_AntialiasingMode"), "temporal AA is allowlisted");
    Require(IsAllowlistedCvar("r_MotionBlur"), "motion blur is allowlisted");
    Require(IsAllowlistedCvar("r_NoDrawNear"), "the viewmodel pass toggle is allowlisted");
    Require(IsAllowlistedCvar("e_CameraFreeze"), "the engine camera override is allowlisted");
    Require(IsAllowlistedCvar("g_detachCamera"), "the detached camera cvar is allowlisted");
    // **Contract changed 2026-09-08, deliberately.** Resolution was excluded
    // because changing it mid-session invalidates the XR swapchain and held eye
    // textures, which are sized once at session start: D3D11 CopyResource needs
    // matching dimensions and does not rescale, so a resize produced a failed or
    // corrupt copy with nothing a player could see.
    //
    // The measurement that forced the change: the headset receives 48% of the
    // pixels its runtime asks for, the deficit almost entirely vertical, and
    // these cvars are the only native levers for it. An exclusion that makes a
    // measured defect unfixable is not a safety property.
    //
    // The hazard is now handled where it actually lives, in the submission
    // path: a backbuffer whose size no longer matches the session refuses to
    // submit and says so, rather than copying mismatched resources. Set these
    // BEFORE xr.start, the same ordering rule gamma follows.
    Require(IsAllowlistedCvar("r_Width"), "render width is allowlisted for the resolution lane");
    Require(IsAllowlistedCvar("r_Height"), "render height is allowlisted");
    Require(IsAllowlistedCvar("r_Supersampling"), "supersampling is allowlisted");
    // Still excluded, and these are the reason the list is fail-closed at all.
    Require(!IsAllowlistedCvar("map"), "level loading is not allowlisted");
    Require(!IsAllowlistedCvar("i_giveitem"), "item spawning is not allowlisted");
    Require(!IsAllowlistedCvar("quit"), "quit is not allowlisted");
    Require(!IsAllowlistedCvar(""), "an empty name is not allowlisted");
}

} // namespace

// The weapon-offset entries added 2026-09-03, pinned deliberately.
//
// These are the first *writable gameplay* entries on the allowlist -- everything
// else is a renderer or debug toggle -- so what they do and do not permit is
// worth stating in tests rather than leaving to a reading of the array.
void TestWeaponOffsetEntriesAreAllowed()
{
    Require(Classify("SetWeaponCameraOffsetX 0.1") == Classification::sceneControl,
        "the weapon camera offset is allowed with a value");
    Require(Classify("SetWeaponCameraOffsetY 0") == Classification::sceneControl,
        "so is Y");
    Require(Classify("SetWeaponCameraOffsetZ -0.05") == Classification::sceneControl,
        "and Z, including a negative value");
    Require(Classify("i_offset_front 0.2") == Classification::sceneControl,
        "the item viewmodel offsets are allowed");
    Require(Classify("i_offset_right") == Classification::query,
        "and readable with no argument, which is how a baseline is captured");
}

// The animation-control entries, added 2026-09-05.
//
// A different kind of widening from the weapon offsets: these do not move
// something, they switch a subsystem off. Worth pinning for that reason -- an
// entry that disables the animator has a much larger blast radius than one that
// nudges a camera, so what it does and does not permit should be stated.
void TestAnimationControlEntriesAreAllowed()
{
    Require(Classify("ca_useADIKTargets 0") == Classification::sceneControl,
        "animation-driven IK targets can be switched off");
    Require(Classify("ca_DebugADIKTargets 1") == Classification::sceneControl,
        "and drawn, which is how the targets get identified by looking");
    Require(Classify("ca_NoAnim 1") == Classification::sceneControl,
        "the animator can be frozen outright");
    Require(Classify("a_poseAlignerEnable 0") == Classification::sceneControl,
        "and Prey's own pose aligner disabled separately");
    Require(Classify("ca_NoAnim") == Classification::query,
        "each is readable with no argument, so a baseline can be captured first");
}

// The animation family is large -- 132 ca_/a_ cvars in the image -- and only four
// are allowed. Prefix matching here would hand over the whole subsystem.
void TestAnimationWideningIsExact()
{
    RequireDenied("ca_DisableAuxPhysics 1",
        "a ca_ cvar that was not added is still denied");
    RequireDenied("ca_UseAimIK 0",
        "so is another plausible-sounding animation toggle");
    RequireDenied("a_poseAlignerForceLock 1",
        "a sibling of an allowed entry is not itself allowed");
    RequireDenied("ca_NoAnimation 1",
        "a name that merely extends an allowed one is denied");
    RequireDenied("ca_NoAnim 1; quit",
        "a separator cannot smuggle a command behind an animation toggle");
}

// Widening the list must not widen anything else. A near-miss name is still
// denied, and the new entries are not an injection vector.
void TestWeaponOffsetWideningIsExact()
{
    RequireDenied("SetWeaponCameraOffsetW 1",
        "a name that merely looks like one of the new entries is still denied");
    RequireDenied("i_offset_back 1",
        "an item offset that was not added is still denied");
    RequireDenied("SetWeaponCameraOffset 1",
        "the prefix without an axis is not on the list");
    // The separator guard has to hold for the new entries exactly as for the old
    // ones -- a writable gameplay command would be a far better smuggling vehicle
    // than a renderer toggle.
    RequireDenied("SetWeaponCameraOffsetX 1; quit",
        "a separator cannot smuggle a second command behind a weapon offset");
    RequireDenied("i_offset_front 1 && exec autoexec.cfg",
        "nor can a shell-style chain");
}

int main()
{
    TestAllowedFormsClassify();
    TestWeaponOffsetEntriesAreAllowed();
    TestWeaponOffsetWideningIsExact();
    TestAnimationControlEntriesAreAllowed();
    TestAnimationWideningIsExact();
    TestCaseInsensitivity();
    TestSeparatorsCannotSmuggleCommands();
    TestExecIsDeniedExplicitly();
    TestUnknownCommandsAreDenied();
    TestMalformedInputIsDenied();
    TestPathAndQuoteCharactersAreDenied();
    TestAllowlistContents();
    std::cout << "PreyVR console policy tests passed\n";
    return 0;
}
