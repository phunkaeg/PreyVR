#pragma once

#include <windows.h>

// Captures each character's real render matrix, so model-space work stops being
// an approximation.
//
// **What it replaces.** The hand lane converts world displacements into model
// space with a *yaw-only body frame* -- camera yaw minus the head's contribution,
// pitch and roll discarded. That is enough for "does the hand follow the
// controller" and it survived a headset, and it is not enough to place a hand
// absolutely, solve an arm chain against real proportions, or put a weapon where
// a controller is.
//
// **The exact transform (R-088).** `CRenderObject+0x00` holds twelve floats, and
// the basis vectors are **columns**: X at `+0x00,+0x10,+0x20`, Y at
// `+0x04,+0x14,+0x24`, Z at `+0x08,+0x18,+0x28`, translation at
// `+0x0C,+0x1C,+0x2C`. `RenderCHR` receives that matrix as its third argument, so
// capturing it at the function's own entry is both simpler than a mid-function
// marker and **earlier than the skinning dispatch** at `0x81D35F` -- which is the
// timing hazard R-088 raises against the existing `0x81D377` observation point.
//
// **The near character is camera-position-relative with world-oriented axes:**
// `Mnear = T(-C) * W`. The basis is *not* rotated by the inverse camera, so for a
// *difference* of two world positions the camera term cancels and only the basis
// matters.
//
// **Absolute placement is fine too, provided the origin belongs to the matrix**
// (R-089). `inverse(M) * (P - C)` reduces to `inverse(W) * P`, so `C` cancels
// exactly -- including the per-eye translation this mod writes. What is *not*
// safe is mixing an origin from one sample with a matrix from another, which is
// what FAIL-HAND-037 actually was. Capture origin, matrix and the active
// `NearViewStereo` eye delta `d` as one coherent tuple: `Ceff = Ceye - d`.
//
// **Two instances of one rig do not share a matrix.** R-085 selects one of exactly
// such a pair, so this is keyed by character and must never be shared between them
// on the strength of a shared rig id.
namespace preyvr::dll {

// Passive: installs the hook and records matrices. Writes nothing.
DWORD SetRenderFrameCapture(unsigned int enabled);

// The most recent render matrix for `character`, as twelve floats in the engine's
// own layout. False when that character has not been seen.
bool TryGetRenderMatrix(unsigned long long character, float out[12]);

// True when the captured basis is orthonormal within tolerance, which is what
// makes an inverse-by-transpose legitimate. R-088 warns that scale or shear needs
// a full affine inverse and an explicit rotation-extraction policy instead, so
// this is checked rather than assumed.
bool RenderMatrixBasisIsOrthonormal(unsigned long long character);

// Whether this character's captured sample was a **near** draw, using RenderCHR's
// own predicate (R-089): `params+0x80 & 0x800000`, or the entity slot's render
// flags at `character+0xAC8 & 2`. That is the test the function performs before
// setting `FOB_NEAREST`, so reading it at entry removes the last injector
// dependency from routine testing.
//
// Near is a **render classification, not proof of player identity** -- a held item
// and a viewmodel are both near. Keep the existing rig and player selection
// alongside it rather than substituting this for them.
bool RenderMatrixIsNear(unsigned long long character);

// --- the per-frame transform seam -------------------------------------------
//
// **This is what `SetAttAbsoluteDefault` could not be.** That writes an
// attachment default the engine samples at attach time, so a mount written every
// frame still does not move the drawn weapon (H-017). `RenderCHR` copies its
// matrix argument into `CRenderObject+0x00` on every draw, so editing it at the
// hook's entry moves the object for that frame and the next one too.
//
// The character is named explicitly. Holding a weapon produces **two** near
// objects, the viewmodel arms and the weapon, so selecting on the near flag
// alone would pick one by draw order.
DWORD SetRenderFrameOverrideCharacter(unsigned long long character);
DWORD SetRenderFrameOffsetMillimetres(int x, int y, int z);
DWORD SetRenderFrameOverrideEnabled(unsigned int enabled);

// Whether the override also moves this character's attachments (H-018).
//
// `ICharacterInstance::Render` draws the character and *then* its attachments,
// composing each against the parent matrix from the same buffer we edit. Off by
// default: the edit is undone after the original returns, so the named character
// moves alone. On, it carries down -- which is how the arms and the weapon in
// them move together.
DWORD SetRenderFrameOverridePropagates(unsigned int propagates);
unsigned long long RenderFrameOverrideAppliedCount();
unsigned long long RenderFrameOverrideRefusedCount();

// One slot of the capture table, for dumping the whole thing over a text channel.
// False when that slot has never been written. This is what lets a live session
// answer "which character is the near one" **without a debugger attached**.
bool RenderFrameSlot(unsigned int index, unsigned long long* character, bool* nearest);

unsigned long long RenderFrameCaptureCount();
unsigned int RenderFrameTrackedCharacters();
// Millionths, for reporting through a text channel without floats.
int RenderFrameBasisMicro(unsigned long long character, unsigned int index);

} // namespace preyvr::dll
