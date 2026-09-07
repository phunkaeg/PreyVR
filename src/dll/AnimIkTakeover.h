#pragma once

#include <windows.h>

// The animation-driven-IK lane (H-021, R-102).
//
// Detours `CSkeletonAnim::ProcessAnimationDrivenIK` (`0x877B50`) and, for the
// first-person hand rig only, writes the controller's wrist into the rig's own
// IK target joint with the weight joint at 1 before the engine solves. The
// engine then does everything the old lanes did by hand and two things they
// could not: it solves the arm, and it runs *before* the bone attachments
// sample the pose, so the weapon follows.
//
// **This lane and `hand.mode 2` are mutually exclusive.** The hand-rig lane
// composes onto the skinned pose after the fact; running both applies the
// controller twice (BioshockVR's rule when its native AimIK took ownership).
//
// Modes: 0 off, 1 observe (identify the rig, read the ADIK table and limbs,
// count calls, write nothing), 2 apply.
namespace preyvr::dll {

DWORD SetAnimIkMode(unsigned int mode);
// Fixed test goal, millimetres, model axes, added to the animated wrist: the
// discriminator that proves the seam without a controller in the loop.
DWORD SetAnimIkTestOffsetMillimetres(int x, int y, int z);
DWORD SetAnimIkControllerDrive(unsigned int enabled);
// Captures, on the next solved frame with a tracked controller, the rotation
// that maps the controller's model-space orientation to the animated wrist's.
DWORD CalibrateAnimIk();
// The rig is selected by signature, never by pointer (F-009): joint count plus
// the presence of an ADIK target named r_hand_spine_target.
DWORD SetAnimIkJointSignature(unsigned int joints);
// 1 right, 2 left, 3 both.
DWORD SetAnimIkHands(unsigned int mask);
// Logs the matched rig's ADIK table and limbs.
DWORD DumpAnimIk();

unsigned int AnimIkMode();
unsigned int AnimIkHooked();
unsigned long long AnimIkCalls();
unsigned long long AnimIkMatched();
unsigned long long AnimIkRigSkeleton();
unsigned int AnimIkRigJoints();
unsigned int AnimIkGate();          // charInst+0x610 on the matched rig
int AnimIkCvar();                   // ca_useADIKTargets
int AnimIkTargetJoint(unsigned int hand);   // 0 right, 1 left; -1 none
int AnimIkWeightJoint(unsigned int hand);
int AnimIkLimbEnd(unsigned int hand);
unsigned int AnimIkLimbTag(unsigned int hand);
unsigned long long AnimIkWritten(unsigned int hand);
unsigned long long AnimIkNoPose();
unsigned long long AnimIkClamped();
unsigned int AnimIkCalibrated(unsigned int hand);
int AnimIkLocationMillimetres(unsigned int axis);
int AnimIkLocationYawMilliDegrees();
int AnimIkLastGoalMillimetres(unsigned int axis);   // right hand, model space

} // namespace preyvr::dll
