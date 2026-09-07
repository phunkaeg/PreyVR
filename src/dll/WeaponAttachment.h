#pragma once

#include <windows.h>
#include <string>
#include "preyvr/RigOwnership.h"

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
// So this uses **`SetAttAbsoluteDefault`**, model-space.
//
// **CORRECTION, R-088: this writes a DEFAULT pose, not the current transform.**
// The attachment update composes it as
//
//     currentMount = J * inverse(B) * absoluteDefault
//
// with `B` the bind joint pose and `J` the current joint pose. Writing a desired
// pose `G` straight in -- which is what this file does -- therefore produces
// `J * inverse(B) * G`, so the weapon **retains the animation's motion** and is
// displaced relative to it rather than placed absolutely.
//
// That is adequate for a controller-driven *delta*, which is what this lane is,
// and it is **not** absolute placement. For that the write must be
// `B * inverse(J) * G`, with `J` taken from the attachment's own owning character
// and its consumed pose -- not from our privately edited hand pose, which the
// attachment update never reads.
//
// Prey also right-multiplies orientation by a quaternion `K` at attachment
// `+0x14C` on the normal static/execute paths, so an exact orientation needs
// `G0 = { qG * inverse(K), tG }`.
//
// **Persistence is confirmed only for the inspected update paths.** They rebuild
// the *relative* default from the absolute one rather than overwriting it. But
// `AlignJointAttachment` (vtable `+0xB8`) does overwrite the absolute default, and
// static analysis could not rule out a per-frame virtual caller -- so a mount that
// stops holding is a known possibility rather than a surprise.
//
// **The vtable is aligned, not assumed.** R-024 recorded months ago that Prey
// installs the weapon binding through attachment vtable slot `+0xD8`; the
// CryEngine interface puts `AddBinding` at index 27, and `27 * 8 == 0xD8`. One
// independent point of agreement is what makes index 9 usable rather than a guess.
namespace preyvr::dll {

using EquippedRig = RigIdentity;
// Revalidates selected equipment and the attachment -> manager -> character
// chain on every read. A skeleton asset signature alone is not ownership.
bool TryGetEquippedRig(std::uintptr_t player, EquippedRig& out);

// Installs the hook on `CArkWeapon::AttachToHand` (R-024) and captures the
// equipped weapon's `IAttachment*` from `CArkWeapon+0x2B0`.
//
// **Capture is passive.** Nothing is written until an offset is armed, so this can
// run while only observing -- which is how every lane that worked today started.
// Passive firing-position observer. Reports a matched sample's actual native
// origin, fallback flag and separation from aim/grip; never replaces firing.
std::string WeaponMuzzleAlignmentReport();

DWORD SetWeaponAttachmentObserving(unsigned int enabled);

// The captured attachment, or 0. Session-specific: re-equip a weapon to refresh it
// rather than carrying one between runs.
unsigned long long WeaponAttachmentPointer();
// The bone the resolved attachment hangs from (`IAttachment+0x15C`, R-102) and
// its binding's simulation flags (`+8+0x28` enabled, `+8+0x2B` redirect) -- a
// spring on the weapon binding lags it behind a fast wrist and, with redirect,
// writes back into the pose. -1 / 0 when nothing is resolved.
int WeaponAttachmentJointIndex();
unsigned int WeaponAttachmentSimulationFlags();

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
unsigned int WeaponOffsetArmed();

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
// **Cross-checked against the tested pure layer.** `WeaponPoseFromController` in
// `preyvr::controller` is `Compose(hand, grip.controllerToWeapon)` -- the same
// `hand * grip` order, with a passing test (`TestWeaponPoseAppliesGrip`). This
// lane composes a *delta from calibration* rather than an absolute pose, because
// the model frame here is approximated rather than solved; when the character's
// true render matrix is captured, the absolute form is the one to move to.
//
// `TwoHandedWeaponPose` also already exists and is the next refinement: Prey's
// disruptor is held with both hands (the support hand sits under the grip, which
// is why limb `0x7E0` tracks it), and a rifle should point where the *supporting*
// hand is rather than where the wrist is angled.
DWORD SetWeaponRotationDrive(unsigned int enabled);

// Records the controller's current aim rotation as the zero, alongside the mount
// captured at equip. Hold the controller as if aiming naturally.
DWORD CalibrateWeaponRotation();

// Called once per animation batch by the hand rig, so the weapon and the hands
// share a cadence as well as a basis -- the playbook's rule that an IK solve
// running at a different rate from the animation it corrects reads as jitter.
void UpdateWeaponMountFromController();

// **The rotation lane's own counters.** They existed as internals and were never
// exposed, so `weaponApplied` -- which counts the *offset* path -- was the only
// visible number and it reads zero whatever the rotation does. A lane that
// cannot be observed cannot be debugged: a live run showed the weapon not
// rotating with no way to tell a missing baseline from an untracked aim pose.
unsigned long long WeaponRotationAppliedCount();
unsigned long long WeaponRotationNoPoseCount();

// The three gates `UpdateWeaponMountFromController` returns on, so a silent
// no-op names itself instead of having to be bisected.
unsigned int WeaponRotationDriveArmed();
unsigned int WeaponRotationCalibrated();
unsigned int WeaponBaselineCaptured();

// Whether the controller's *aim* pose is usable. The hand lane uses grip and is
// proven working; the weapon lane uses aim, and the two have independent
// validity flags.
unsigned int WeaponAimPoseUsable();

unsigned long long WeaponOffsetAppliedCount();
// Writes refused because the mount did not look like a plausible `QuatT` -- a
// non-unit quaternion or a non-finite translation. Non-zero means the pointer is
// not what we think it is, which is a different fault from the hook not running.
unsigned long long WeaponOffsetRefusedCount();

} // namespace preyvr::dll
