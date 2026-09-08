#pragma once

#include <windows.h>

// Gives the near/viewmodel pass a per-eye viewpoint, which it structurally
// cannot have on its own.
//
// **The problem, confirmed in the binary rather than inferred.** Prey builds a
// **translation-free** view matrix for near geometry -- `0xFB0B70` explicitly
// zeroes the translation row at `0xFB1037`/`0xFB103B` -- and the near
// view-projection at view-info `+0xA0` is that zero-translation view multiplied by
// the near projection. So the weapon is drawn camera-relative by construction, and
// **no amount of moving the camera can give it stereo separation**. A wearer found
// the symptom first: the weapon identical in both eyes while the shadows it casts
// separate correctly, because shadows are computed in world space.
//
// **Why the edit goes here and not at the translation clear.** Patching
// `0xFB1037` would restore translation to *every* camera-relative product, not
// just this pass. The narrow edit is the finished near VP, after its builder has
// run and before it is packed for upload.
//
// **The transform.** For the engine's row-vector layout, with `M0` the original
// near VP and `d` the eye-minus-cyclops displacement in engine units:
//
//     M[3][j] = M0[3][j] - d.x*M0[0][j] - d.y*M0[1][j] - d.z*M0[2][j]
//
// Rows 0..2 are untouched. This is `Translation(-d) * M0`.
//
// **Eye-only displacement, not the camera position.** Near vertices already have a
// camera-relative origin; adding the world camera position would move them into
// the world twice. Half the IPD along the camera's right axis preserves the origin
// while giving the two eyes different viewpoints.
//
// **Snapshot and restore around the call**, so nothing accumulates and no other
// consumer of that view-info sees an edited matrix -- the same discipline the
// camera edit uses. The value is therefore always computed from an unedited
// original, which is what makes the zero-delta control meaningful.
namespace preyvr::dll {

// Arms the edit. `halfIpdMetres` is half the interpupillary distance; the sign is
// taken from which eye the camera hook last published, so the two eyes get
// opposite displacements.
//
// Returns 0 armed, 1 landmark missing, 2 prologue mismatch, 3 hook failed.
DWORD SetNearViewStereo(unsigned int enabled);

// Half-IPD in **millimetres**, so it crosses as an integer. 32 is half of a 64 mm
// IPD, the value a wearer chose for the world stereo.
DWORD SetNearViewHalfIpdMillimetres(unsigned int millimetres);

// **The zero-delta control.** Arms every code path -- hook, snapshot, matrix
// arithmetic, restore -- with a displacement of zero, so a *correct*
// implementation is pixel-identical to no edit at all. Any visible change under
// this flag is the edit itself being wrong, not stereo appearing.
DWORD SetNearViewZeroDeltaControl(unsigned int enabled);

// Frames where the matrix was edited, and where it was refused. A refusal means
// the near VP did not look like a plausible projection, which is the guard against
// editing a view-info that is not the one this is meant for -- several callers
// build temporary view-info and share the same packer.
unsigned long long NearViewAppliedCount();
unsigned long long NearViewRefusedCount();

// Frames where the eye was unknown, so no signed displacement could be chosen.
// Non-zero with applied at zero means the camera hook is not publishing an eye,
// which is a different fault from the hook not running.
unsigned long long NearViewNoEyeCount();
// Nested calls that found the view projection already offset for this eye and
// forwarded it untouched. Non-zero means the engine really does re-enter the
// packer, and each one would have been a doubled weapon offset before the guard.
unsigned long long NearViewReenteredCount();
// Calls that found the buffer already carrying the exact row we last wrote for
// this eye -- a racing render-job thread, or a copy of an edited matrix -- and
// forwarded it rather than offsetting it a second time. Each one would have been
// a doubled weapon offset. Expect this to rise with scene complexity.
unsigned long long NearViewAlreadyOffsetCount();
// Packer calls refused because the view-info's own camera could not be matched
// to a published eye record. This is the fail-closed path that replaces reading
// a mutable game-thread eye global: an un-offset near pass is a visible, smaller
// error than a confidently wrong eye. Climbing means the eye records are not
// reaching the render thread, not that the offset is wrong.
unsigned long long NearViewNoProvenanceCount();

// Records the distinct view-info pointers the near hook edits and the
// translation row it FOUND on each before editing. A second pointer whose found
// row already differs by the half-IPD is a copy of an edited matrix, which is
// the remaining explanation for H-022's doubled offset.
DWORD ArmNearViewLineage(unsigned int enabled);
DWORD DumpNearViewLineage();

// Last displacement actually applied, in micrometres, signed. This is the number
// that says the two eyes are getting *opposite* offsets rather than the same one.
int NearViewLastDeltaMicrometres();

} // namespace preyvr::dll
