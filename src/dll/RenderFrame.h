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
// matters. For absolute placement the camera origin `C` must be matched to the
// sample that built the matrix.
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

unsigned long long RenderFrameCaptureCount();
unsigned int RenderFrameTrackedCharacters();
// Millionths, for reporting through a text channel without floats.
int RenderFrameBasisMicro(unsigned long long character, unsigned int index);

} // namespace preyvr::dll
