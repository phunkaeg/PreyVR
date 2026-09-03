#pragma once

#include <string_view>

// What an automated test is allowed to type into Prey's console.
//
// Driving the console is the right way to script this game -- it is the engine's
// own supported interface, it is deterministic, and it beats clicking through
// menus with screen coordinates. But `ExecuteString` will run *anything*, and an
// automation path that can run anything is one typo away from saving over a
// profile or executing a config file off disk.
//
// So the rule here is the same one the landmark gate uses: **allowlist, and fail
// closed.** An unrecognised command is denied, not passed through with a
// warning. The set below is small on purpose -- every entry earns its place by
// being needed for a specific test, and the comment says which.
//
// Pure policy, no engine dependency, so it is decided and tested headless.
namespace preyvr::console {

enum class Classification {
    denied = 0,      // not recognised, or actively unsafe -- do not execute
    query = 1,       // a bare cvar name; the engine prints its value, changes nothing
    sceneControl = 2 // an allowlisted assignment used to make a scene deterministic
};

// `command` is the raw line as it would be typed. Leading and trailing
// whitespace is tolerated; everything else must be exact.
//
// Denied unconditionally, before the allowlist is even consulted:
//   - empty or whitespace-only, or longer than kMaxCommandLength
//   - anything containing a newline, carriage return, or ';' -- CryEngine treats
//     these as command separators, so allowing them would let one approved
//     command smuggle an unapproved one behind it
//   - anything containing '"' or a path separator, which is how file-touching
//     commands are written
//   - `exec` in any casing: ExecuteString special-cases it to run a file from
//     disk, and it is the one command that bypasses deferred execution
//   - anything whose first token is not in the allowlist
//
// A `query` must be a bare name with no argument. An assignment must be exactly
// `<name> <value>` with a single argument that parses as a number -- string
// cvars are not in scope for scene setup and allowing arbitrary string values
// would reopen the injection surface that the separator ban closes.
Classification Classify(std::string_view command);

// Long enough for `<name> <float>`, short enough that nothing interesting fits.
inline constexpr std::size_t kMaxCommandLength = 96;

// True if `name` is an allowlisted cvar. Exposed so a caller can check a name
// before reading its current value for restoration -- which it must do: every
// sceneControl command changes game state, and the bounded-write discipline
// this project uses requires recording the prior value and putting it back.
bool IsAllowlistedCvar(std::string_view name);

// The allowlist, and why each entry is on it:
//
//   t_Scale                      freeze the simulation. This is the load-bearing
//                                one -- comparing two frames is meaningless while
//                                NPCs, physics and particles are still moving.
//   r_AntialiasingMode           disable temporal AA. Live value 3, and TAA makes
//                                a static scene differ frame to frame, which is
//                                exactly the noise a camera-move test must not be
//                                confounded by.
//   r_MotionBlur                 same reason; live value 2.
//
//   SetWeaponCameraOffsetX/Y/Z   move the first-person weapon camera offset.
//   i_offset_front/right/up      CryEngine's standard item viewmodel offsets.
//
//     **Added 2026-09-03 on the owner's explicit decision, and they are the first
//     writable gameplay entries in this list** -- everything else here is a
//     renderer or debug toggle. The reason is H-005: the fleet graph surfaced
//     FarCry2-vr finding Dunia's weapon camera offset, which moves the weapon with
//     no render-pass hook and no engine call, and PreyDll.dll carries the same
//     family. One live test settles whether Prey's are consumed, and F-007 is the
//     reason a live test is needed rather than a symbol search: the sweep that
//     found no consumer for g_detachCamera also found none for g_difficultyLevel,
//     which certainly is consumed.
//
//     Both are reversible by setting them back, and neither reaches outside the
//     game. Widening this list is still a deliberate act, not a convenience, and
//     the fail-closed default stands for everything not named here.
//   sys_MaxFPS, r_VSync          pace the host so a capture is not racing present.
//   e_TimeOfDaySpeed             stop the sky and lighting drifting between frames.
//   r_DrawNearFoV, r_NoDrawNear  the viewmodel pass. r_DrawNearFoV is live at 54
//                                against the world's 88, and r_NoDrawNear turns
//                                the pass off outright -- both needed to work out
//                                what happens to the weapon under a per-eye camera.
//   e_CameraFreeze,
//   e_CoverageBufferDebugFreeze  the engine-honoured override found in
//                                UpdateRenderingCamera: together they switch the
//                                render camera to GetViewCamera() while freezing
//                                culling. Worth being able to toggle deliberately.
//   g_detachCamera               the detached-camera cvar family; still unproven,
//                                and one console command settles whether it is
//                                live or vestigial.
//   e_Recursion                  the recursive render views are allocated and idle;
//                                this is the switch that would wake them.

} // namespace preyvr::console
