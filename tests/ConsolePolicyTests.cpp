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
    Require(!IsAllowlistedCvar("r_Width"), "resolution is not allowlisted");
    Require(!IsAllowlistedCvar(""), "an empty name is not allowlisted");
}

} // namespace

int main()
{
    TestAllowedFormsClassify();
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
