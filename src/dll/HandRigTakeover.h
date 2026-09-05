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

} // namespace preyvr::dll
