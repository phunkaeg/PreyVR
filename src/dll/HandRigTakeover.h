#pragma once

#include <windows.h>

// Independent per-hand control, taken at the pose **consumer** rather than the
// producer.
//
// **Why here.** Three levels of the producer side were walked (R-082, R-083) and
// every one is recomputed each frame, so a write anywhere upstream is overwritten
// before use -- six live bump attempts confirmed it. `CCharInstance::Skinning`
// `TransformationsComputation` (`0x82EE10`) is the other end: it reads the
// finished absolute joint poses, converts them against the inverse bind pose into
// dual quaternions and publishes. Nothing recomputes after it.
//
// **The seam is the input, not the output.** We hand the original function a
// private copy of the pose input with chosen joints replaced, and let it do the
// native conversion. Patching its *output* would be too late: publication happens
// **inside** the function (an atomic exchange at `0x82F860`, software jobs started
// at `0x82F88B`), so software consumers may already be running by the time it
// returns.
//
// **Call the original exactly once per scheduled job.** Invoking it twice on the
// same skinning packet to compare A against B is not a valid experiment -- it
// repeats the list-publication protocol after that state has already changed.
//
// **A wrist alone tears the hand.** A rigid delta must be applied to the selected
// joint *and every descendant*, or fingers stay where they were while the wrist
// moves.
namespace preyvr::dll {

// 0 off, 1 passthrough, 2 apply.
//
// **Passthrough is the control and it is not a no-op.** It exercises the hook, the
// joint copy, the private pose-data view and the forwarded call, with the joints
// unchanged -- so a correct implementation is pixel-identical to no hook at all.
// Any visible change in passthrough is this code being wrong rather than the
// takeover working.
DWORD SetHandRigTakeoverMode(unsigned int mode);

// Which joint's subtree moves. Defaults to none; nothing is written until a joint
// is chosen deliberately, because indices 45 and 72 are *end-effector candidates*
// and may be `*IKTarget` helpers rather than deforming wrists. Resolve the name
// from the topology dump first.
DWORD SetHandRigJoint(unsigned int jointIndex);

// Model-space offset applied to that subtree, in millimetres, signed. The first
// discriminator in the investigation's protocol is a fixed 100 mm shift.
DWORD SetHandRigOffsetMillimetres(int x, int y, int z);

// Restricts the edit to one character instance.
//
// **Necessary because the rig is instanced twice.** R-085 measured two character
// instances sharing the 101-joint hand rig: one drawn with `FOB_NEAREST` (the
// first-person hands) and one without (the world body that casts the shadow),
// 949 draws each per four seconds. Converting both would move the shadow as well,
// which is not the product -- and would also destroy the best control available
// here, since **the shadow not moving is what proves the right instance was
// picked**.
//
// `FOB_NEAREST` is a *per-draw* render-object flag, while this hook is *per
// character*, so the correlation cannot be made here. The observer that watches
// `RenderCHR` supplies the pointer instead.
//
// Zero means "any character", which is only appropriate for topology capture.
DWORD SetHandRigCharacterPtr(void* character);

// The last character converted, so an observed draw can be joined to a
// conversion by pointer.
unsigned long long HandRigLastCharacter();

// Conversions that matched the selected character, and those skipped because they
// did not. **Skipped climbing while matched stays zero means the selection is
// wrong** -- a different fault from the hook not running, and the reason these are
// counted apart.
unsigned long long HandRigMatchedCount();
unsigned long long HandRigSkippedCount();

// --- Controller drive -----------------------------------------------------
//
// Both hands, independently: the right controller drives one joint's subtree and
// the left drives another. That independence is the whole point -- the near-render
// camera could have moved the viewmodel far more easily, and was rejected because
// it moves both arms and the body together.

// Which joint each hand drives. Defaults to none; set from the topology dump,
// **by name**, since indices are not portable between skeletons (R-084).
DWORD SetHandRigRightJoint(unsigned int jointIndex);
DWORD SetHandRigLeftJoint(unsigned int jointIndex);

// 0 = the fixed offset used for the first discriminator, 1 = controller-driven.
DWORD SetHandRigControllerDrive(unsigned int enabled);

// Wrist rotation, armed separately from the positional drive.
//
// **Separate because they fail differently.** A wrong position is a hand in the
// wrong place and is obvious; a wrong rotation is a hand that looks broken and
// is easy to misread as the takeover being wrong altogether. Arming them one at
// a time keeps the positional lane -- proven exact to the millimetre -- as a
// control while the rotation is brought up.
DWORD SetHandRigWristDrive(unsigned int enabled);

// A constant yaw, in tenths of a degree, between the body frame and the frame the
// bone rotations live in. See `WorldTurnToModel`: the body-yaw conversion is
// required regardless, and this is the residual rig convention on top of it,
// left tunable so a wearer can settle it in one session instead of a rebuild.
DWORD SetHandRigTurnYaw(int deciDegrees);
int HandRigTurnYawDeciDegrees();
unsigned long long HandRigWristAppliedCount();
unsigned int HandRigWristDriveArmed();

// Records where the controllers are **now** as the zero point, so the hands stay
// where the animation put them until you move. Without this the first frame would
// snap the hands to wherever the controllers happen to be relative to an
// arbitrary origin.
//
// Call it with your hands in a comfortable neutral pose.
DWORD CalibrateHandRig();

// Displacement scale in percent; 100 is one-to-one. Exists because a hand that
// moves too little or too much is judged in a headset, not here.
DWORD SetHandRigScalePercent(unsigned int percent);

// Last applied displacement per hand, in millimetres, so a hand that is not
// moving can be told apart from a hand whose delta is not reaching the seam.
int HandRigLastRightMillimetres();
int HandRigLastLeftMillimetres();
// Conversions where a controller pose was unusable -- untracked, stale, or before
// calibration. The hands keep the engine's animation on those frames.
unsigned long long HandRigNoPoseCount();

// --- Topology, captured passively at the consumer -------------------------
//
// Step one of the protocol, and it must happen before any write: the joint whose
// name sounds relevant is often a helper, not the one that deforms the mesh.

// Joints seen on the last conversion, and the model path of that skeleton.
DWORD HandRigJointCount();
// Copies joint `index`'s name into `buffer`. Returns the number of bytes written.
DWORD HandRigJointNamePtr(void* request);
// Signed parent index; -1 is the root.
int HandRigJointParent(unsigned int index);

// --- Health ---------------------------------------------------------------

unsigned long long HandRigForwardedCount();   // passthrough, joints copied unchanged
unsigned long long HandRigAppliedCount();     // a subtree was displaced
// Conversions refused: implausible joint count, null pose array, non-finite
// transforms, or a subtree that would not fit the scratch. Non-zero with applied
// at zero means the guard is rejecting every call, which is a different fault from
// the hook not running.
unsigned long long HandRigRefusedCount();
// How many joints the last applied delta moved. One means only the selected joint
// moved and the descendant walk found nothing -- which would tear the hand.
DWORD HandRigLastSubtreeSize();

// **The armed configuration, because its absence let a control run be read as a
// broken feature.** Passthrough (mode 1) leaves every displacement counter at
// zero by design, which is indistinguishable from mode 2 with a dead controller
// -- and `handNoPose=0` was once cited as evidence a controller pose had arrived
// when the drive block had not executed at all. Reporting the mode, the drive
// flag and whether calibration has happened makes "nothing moved" separable from
// "nothing was asked to move".
unsigned int HandRigMode();
unsigned int HandRigControllerDriveArmed();
unsigned int HandRigCalibrationDone();
// **The two operands of the subtraction, not just its result.** `handRightMm`
// alone cannot distinguish a stale calibration zero from a reference frame that
// moved between calibrating and sampling — H-016 has two live suspects and one
// number. These expose the calibration zero, the current sampled world position
// and the yaw each was taken at, in millimetres and millidegrees, so the delta
// is decomposable rather than merely surprising.
int HandRigZeroRightMm(unsigned int axis);
int HandRigWorldRightMm(unsigned int axis);
int HandRigCalibrationYawMilli();
int HandRigLastYawMilli();

int HandRigSelectedRightJoint();
int HandRigSelectedLeftJoint();

} // namespace preyvr::dll
