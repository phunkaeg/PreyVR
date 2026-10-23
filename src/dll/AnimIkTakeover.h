#pragma once

#include <string>

#include <windows.h>

// The animation-driven-IK lane (H-021, R-102).
//
// Detours `CSkeletonAnim::ProcessAnimationDrivenIK` (`0x877B50`) and, for the
// first-person hand rig only, writes the controller's wrist into the rig's own
// IK target joint with the weight joint at 1 before the engine solves. The
// engine then does everything the old lanes did by hand and two things they
// could not: it solves the arm, and it runs *before* the bone attachments
// sample the pose, so the weapon follows. Weight 1 selects the full goal and
// rotation blend; it does not lift the native solver's reach, stretch,
// singularity and angle clamps (H-018), so the goal is clamped to the authored
// reach here and the wrist is placed within those limits, not exactly.
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
// Opt-in authored weapon-to-aim orientation. Right arm only; mode 0 uses the
// existing grip calibration. Mode 1 refuses unsupported bindings/helpers and
// never captures the controller's equip-time angle. Position/reach are unchanged.
DWORD SetAnimIkWeaponAlignment(unsigned int enabled);
std::string AnimIkWeaponAlignmentReport();
// Captures, on the next solved frame with a tracked controller, the rotation
// that maps the controller's model-space orientation to the animated wrist's.
// With ik.align 1, this request applies only to the left hand.
DWORD CalibrateAnimIk();
// Asset signature is checked inside the selected weapon's live owning character.
// Re-equip after observation is installed; each attach invalidates calibration.
DWORD SetAnimIkJointSignature(unsigned int joints);
// 1 right, 2 left, 3 both.
// **One coherent per-hand record from inside the IK callback**, off by default.
//
// The static audit (docs/RE-IK-CROSSTALK-2026-09-09.md) names two candidate
// paths for the right controller dragging the left hand, and no existing counter
// separates them: the shared anchor is the native cached RETICLE origin, which
// our own reticle write moves; and reach compression retains (1-k) of the
// animated shoulder, so a stationary controller still yields a moving goal.
//
// Latest-value counters cannot settle this. Read one at a time they may describe
// different frames, and the question is which term in ONE frame's arithmetic
// moved. This publishes anchor, yaw, head, grip, character location, world
// shoulder and all three goal stages -- raw, scaled, clamped -- per hand, with
// the tracking sequence that produced them.
//
// Read it with only the right controller moving: whichever of `ikAnchorL` or
// `ikShoulderL` tracks that motion is the path, and if `ikGoalRawL` is static
// while `ikGoalScaledL` moves, compression is responsible rather than the anchor.
// **The crosstalk fix.** 1 (default) anchors both hands at the view camera's
// centre; 0 restores the reticle-derived anchor so a wearer can A/B them.
// `nativeEye` is the native cached RETICLE ray origin, which our own reticle
// lane moves, so it carried the right controller's aim into both hands
// (RE-IK-CROSSTALK-2026-09-09). Refuses the frame when the camera cannot be
// read rather than falling back, since a silent fallback restores the defect.
DWORD SetAnimIkCameraAnchor(unsigned int enabled);
unsigned long long AnimIkAnchorCameraCount();
unsigned long long AnimIkAnchorReticleCount();
unsigned long long AnimIkAnchorMissingCount();

DWORD SetAnimIkTrace(unsigned int enabled);
std::string AnimIkTraceReport();

DWORD SetAnimIkHands(unsigned int mask);
// Scales the goal's distance from the shoulder, 50..150, default 100. Below 100
// maps a longer-armed player onto a shorter character arm so the hand keeps
// moving instead of stopping at the clamp. A preference, not a correctness fix.
DWORD SetAnimIkReachPercent(int percent);
int AnimIkReachPercent();
// Logs the matched rig's ADIK table and limbs.
DWORD DumpAnimIk();
// Logs, once, every joint of the owning rig: name, parent (derived from the
// pose), relative pose and absolute pose before/after the native ADIK pass.
DWORD RequestAnimIkSkeletonDump();
// ik.helpers: logs the held weapon's attachments, joints and bounds on the next
// alignment pass (diagnostic, read-only).
DWORD RequestAnimIkHelperDump();
// ik.widesupport: accept a two-handed foregrip up to 45 cm beside and 80 cm ahead
// of the barrel (default 1; the Q-Beam's), or only 25/70 cm (0, the original).
DWORD SetAnimIkWideSupport(unsigned enabled);
unsigned AnimIkWideSupport();
// ik.supportlatch: the two-handed support region is one settled socket per
// weapon (1, default) or the raw animated native palm every frame (0, A/B).
DWORD SetAnimIkSupportLatch(unsigned enabled);
// ik.longreach [on comfortMm maxBackMm supportBackMm twistDeg]: a weapon whose
// foregrip lies beyond comfortMm ahead of the trigger wrist is held back by the
// difference (at most maxBackMm), its grab region reaches supportBackMm behind
// the foregrip, and the drawn torso turns twistDeg while it is held two-handed.
DWORD SetAnimIkLongReach(int enabled, int comfortMm, int maxBackMm, int supportBackMm, int twistDeg);
std::string AnimIkLongReachReport();
unsigned AnimIkSupportLatch();

// **The VR hand pose** (jordi/hand-pose; see the pose.* notes in the .cpp).
// Mode 1 (default): the free hand's orientation is anatomical (the game hand's
// frame on the real hand's, from the grip pose), the palm -- not the wrist --
// goes to the controller on both hands, the free hand holds one relaxed open
// pose and takes the native two-handed hold on the weapon while gripping.
// Mode 0: the original lane (calibrated offset, wrist at the grip).
DWORD SetHandPoseMode(unsigned int mode);
// Relaxed flexion, percent (100 default, 0 straight fingers).
DWORD SetHandPoseCurlPercent(int percent);
// The grip point in the hand (fraction of the wrist -> knuckle distance in
// permille, millimetres out of the palm and towards the thumb) and the fist's
// tube lean in tenths of a degree.
DWORD SetHandPoseGrip(int alongPermille, int outOfPalmMm, int towardThumbMm, int leanDeciDegrees);
// 0: the linear reach percent (ik.reach); 1: exact up to knee% of the arm's
// reach, then a smooth approach to it.
DWORD SetHandPoseReach(unsigned int mode, int kneePercent);
// 1 (default): a held weapon is rolled about its barrel (the aim ray) until
// the hand holding it lies like the real hand; 0: the muzzle helper's roll.
DWORD SetHandPoseWeaponRoll(unsigned int enabled);
// The reach limit over the rig's arm length, percent (90..125, default 120).
DWORD SetHandPoseStretchPercent(int percent);
// The arm from the player's shoulder (docs/ARM-POSE-2026-10-06.md), arm.<what>:
//   mode 0|1                     native arm / player's shoulder (default 1)
//   shoulder <fwd> <out> <up>    shoulder joint from the centre eye, torso frame, mm
//   len <upper> <fore>           drawn segments, mm (default 285 275)
//   stretch <clav> <arm>         permille the clavicle may stretch, arm stretch limit (150 1100)
//   prior <flexSd> <devRest> <devSd> <swivelSd>  the elbow's MAP priors, degrees (45 -8 12 25)
//   rom <flex> <ext> <radial> <ulnar>            the wrist's range of motion, degrees (75 65 20 35)
//   hold <sd> <tauMs>            temporal prior on the elbow (deg) and pole smoothing (45 60)
//   torso <deadzoneDeg> <relax>  torso yaw deadzone and relax per second, permille (35 600)
//   knee <permille>              exact reach up to this fraction of the stretched arm (920)
DWORD SetArmSetting(const std::string& what, const int* values, unsigned count);
std::string ArmReport();
// Settings, the measured error of each hand against the controller (palm
// position, hand axes) and the grip pose relative to the aim pose.
std::string HandPoseReport();
// The hands as drawn, as segments in the app's OpenXR space, one per line:
// "x1 y1 z1 x2 y2 z2 r g b" (the mock compositor draws them).
std::string HandPoseMarks();

// Last accepted identity/sample, and failed prerequisite/lock attempts.
unsigned long long AnimIkOwnerCharacter();
unsigned long long AnimIkOwnerGeneration();
unsigned long long AnimIkPoseSequence();
unsigned long long AnimIkNoOwner();
// arms.free: with no weapon drawn (holstered, carrying) the player's own arms
// rig is driven as well (open hands; the carrying hand closes).
DWORD SetAnimIkFreeArms(unsigned enabled);
unsigned AnimIkFreeArms();
// The IK is driving the no-weapon arms right now.
bool AnimIkFreeArmsBound();
std::string AnimIkFreeArmsReport();
unsigned long long AnimIkBusy();
unsigned int AnimIkMode();
unsigned int AnimIkHooked();
unsigned long long AnimIkCalls();
unsigned long long AnimIkMatched();
unsigned long long AnimIkRigSkeleton();
unsigned int AnimIkRigJoints();
// charInst+0x610 on the matched rig: the animation object's nonempty-command
// predicate (CSkeletonAnim+0x4D0, m_IsAnimPlaying), which the ADIK pass tests.
// Zero means no animation commands this frame, not "no IK targets".
unsigned int AnimIkGate();
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
