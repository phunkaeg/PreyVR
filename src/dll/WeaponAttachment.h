#pragma once

#include <windows.h>

// Moves the weapon model itself, independently of the hands.
//
// **Why this is a separate lane from the hand rig.** Displacing the hand joints
// at the skinning consumer moves the *hand mesh* and nothing else -- confirmed in
// a headset (R-086): the right hand moved 200 mm and the wrench stayed exactly
// where it was. The weapon is a bound attachment, and its transform is computed
// from the pose by the attachment system, not from the skinning input we
// substitute.
//
// **The route, and why it is this one.** The fleet playbook's taxonomy of ways to
// put an object in a hand gives three, and for an object the engine already has
// attached the answer is **kinematic, local** -- animate the object in its own
// frame, which needs no correction and cannot fight the world. RE4VR takes the
// same route.
//
// **`SetAttRelativeDefault` is a trap, and CryEngine says so in its own header:**
// *"This feature is currently missing. The location set by this method is being
// overridden by the animation update. Please compute the location manually and
// use the SetAttAbsoluteDefault() method instead as a workaround."* That is the
// same producer-overwrite pattern this project hit twice (R-083, R-087) -- here
// documented in advance rather than discovered by losing a session.
//
// So this uses **`SetAttAbsoluteDefault`**, model-space, computed manually.
//
// **The vtable is aligned, not assumed.** R-024 recorded months ago that Prey
// installs the weapon binding through attachment vtable slot `+0xD8`; the
// CryEngine interface puts `AddBinding` at index 27, and `27 * 8 == 0xD8`. One
// independent point of agreement is what makes index 9 usable rather than a guess.
namespace preyvr::dll {

// Installs the hook on `CArkWeapon::AttachToHand` (R-024) and captures the
// equipped weapon's `IAttachment*` from `CArkWeapon+0x2B0`.
//
// **Capture is passive.** Nothing is written until an offset is armed, so this can
// run while only observing -- which is how every lane that worked today started.
DWORD SetWeaponAttachmentObserving(unsigned int enabled);

// The captured attachment, or 0. Session-specific: re-equip a weapon to refresh it
// rather than carrying one between runs.
unsigned long long WeaponAttachmentPointer();

// The attachment's current model-space mount, in millimetres and milli-units, read
// through `GetAttAbsoluteDefault`. This is the number to look at before writing
// one: an offset applied to a mount you have not read is an offset applied to
// nothing in particular.
int WeaponMountPositionMillimetres(unsigned int axis);   // 0 x, 1 y, 2 z
int WeaponMountQuaternionMilli(unsigned int component);  // 0 x, 1 y, 2 z, 3 w

// Arms a model-space displacement of the weapon mount, in millimetres. Applied on
// top of the mount captured at arm time, so disarming restores exactly.
DWORD SetWeaponOffsetMillimetres(int x, int y, int z);
DWORD SetWeaponOffsetEnabled(unsigned int enabled);

// --- Controller-driven rotation -------------------------------------------
//
// **Driven from the same `aimPose` the aim lane uses**, so the weapon points where
// the shots go. The playbook is explicit that the visible model and the aim ray
// are separate surfaces that must not be retuned against each other -- but they
// should *share one basis*, and this is that basis.
//
// **Composition order is `currentController * authoredMount`.** The captured
// mount is a basis change applied BEFORE the tracked delta, not added after.
// Getting this backwards produces a symptom that looks exactly like a sign error
// and cannot be fixed by flipping signs; BioshockVR lost time to it with a
// correctly-loaded offset composed on the wrong side.
//
// The diagnostic, recorded here so it is not rediscovered: **if both extremes of a
// signed parameter fail symmetrically, the model is wrong, not the sign.** Stop
// bisecting and re-derive which transform the value belongs to.
DWORD SetWeaponRotationDrive(unsigned int enabled);

// Records the controller's current aim rotation as the zero, alongside the mount
// captured at equip. Hold the controller as if aiming naturally.
DWORD CalibrateWeaponRotation();

// Called once per animation batch by the hand rig, so the weapon and the hands
// share a cadence as well as a basis -- the playbook's rule that an IK solve
// running at a different rate from the animation it corrects reads as jitter.
void UpdateWeaponMountFromController();

unsigned long long WeaponOffsetAppliedCount();
// Writes refused because the mount did not look like a plausible `QuatT` -- a
// non-unit quaternion or a non-finite translation. Non-zero means the pointer is
// not what we think it is, which is a different fault from the hook not running.
unsigned long long WeaponOffsetRefusedCount();

} // namespace preyvr::dll
