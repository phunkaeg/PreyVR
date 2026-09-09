# Vee.ViewmodelTweaks: collision and alignment code worth adapting

Inspected upstream commit `88d62d85b2adcdd810815390ccdf6ec154c8a734`, dated
2026-09-08T20:46:51Z, from
[Vee.ViewmodelTweaks](https://github.com/vittorioromeo/Vee.ViewmodelTweaks).
The exact source, README, notes and MIT license are preserved under
`docs/evidence/vee-viewmodel-review-2026-09-09/upstream/`; hashes and immutable
download URLs are in `upstream-identity.json`.

This is SOURCE evidence about upstream code, not runtime/headset acceptance or
a port to our Steam build. PreyVR source checked at
`1f5a02851257a1f8a339e924db7ad6bf3e6a2e51`. No game process was touched and no
production behavior was changed. The web renderer could read the README but
not the raw file; a read-only public GitHub download supplied the pinned source.

## What the wall feature actually does

`Src/ModMain.cpp:1176`, `UpdateConvergence`, performs one synchronous
`RayWorldIntersection` from the global view camera along camera +Y. The comment
explicitly identifies this as the previous frame's rendered camera. It skips
the player's physical entity, requests one hit, uses `ent_all`, and supplies
`rwi_stop_at_pierceable | rwi_colltype_any(geom_colltype_ray | geom_colltype0 |
geom_colltype_player)`. The maximum ray distance is clamped to 1..200 units.
Those enum values and the physics ABI still need verification in our target;
their names and Chairloader headers are not portable offsets/layout proof.

The shared ray powers three effects:

1. Hip-fire convergence estimates yaw/pitch from the camera-relative weapon
   offset and the hit distance, then limits/smooths those angles.
2. Wall pull-back maps hit distance through a smoothstep between start/full
   thresholds, multiplies by a per-weapon maximum, clamps that maximum to
   0..0.5 m, and exponentially smooths the resulting displacement. One time
   constant governs both entering and leaving the obstruction.
3. Aim blocking compares distance with an estimated reach:
   `max(hipRelCam.forward, 0.15) + perWeaponWallPush`, multiplied by tolerance.
   Once blocked, it remains blocked until clearance exceeds 110% of the entry
   threshold. That hysteresis applies to the aim-block state, not the ray or
   the pull-back displacement.

Pull-back subtracts view-space Y from the hip offset (`:499`) and optionally
from the aimed pose (`ComputeAimExtra`, `:606`). The guard is consumed by the
ironsight state at `:1391`. It does not itself establish that firing is blocked:
the shown FireWeapon hook calls the original first (`:333`) and then reports
the shot. Do not reinterpret "no aiming" as "no shooting through geometry".

The built-in table (`:1720`) provides, for example, maximum retractions of
0.066 m for the pistol, 0.134 m for the shotgun and 0.157 m for GLOO. These are
tuned presentation amounts, not measured weapon lengths or collision hulls.

## Why the geometry query must change for VR

The implementation is useful camera-based viewmodel avoidance. It does not
test the full weapon mesh, a swept weapon volume, or arbitrary tracked hand
motion in the inspected path.

Two discriminating VR cases are straightforward:

- Look through a clear doorway while holding the gun sideways into the jamb.
  The camera ray can miss while the weapon intersects geometry.
- Look at a wall while holding the gun out through the doorway. The camera ray
  can hit while the weapon has clearance.

These are geometric consequences of the different query origin/path, not newly
run in-game tests. A stationary barrel ray also does not cover sideways motion,
weapon thickness or a fast rotation between frames.

For PreyVR, adapt the scene-query entry, filtering lessons and response policy
to a world-space weapon pose after grip/aim alignment. Start with a bounded
weapon volume such as a capsule/short chain of spheres, and sweep it from the
previous accepted pose to the desired current pose, with explicit handling for
initial overlap, rotation and discontinuous tracking/recenter. A small ray set
could be an initial approximation, but retain that coverage limitation.
Prove the Steam physics receiver, ABI, result layout and filter constants before
calling; padding alone cannot validate a native output structure.

Keep tracked input unchanged. Collision produces a constrained visual weapon
pose and a blocked state; IK follows the constrained grip. Do not correct the
headset/camera or drag the free left hand with the right weapon. Two-hand
coupling needs an explicit grip state, not a proximity-only heuristic.

Use one final transaction:

```text
tracked grip + authoritative aim + authored weapon transforms
    -> desired pose, including any deliberate recoil/two-hand policy
    -> collision constraint, reconciled with IK reach/contact constraints
    -> final weapon/grip/muzzle pose and validity
    -> owning rig write + firing/interaction policy + reticle projection
```

Revalidate final clearance if IK clamping or a later pose modifier changes it.
Do scene queries once for the relevant simulation/pose sample, not once per eye.
Use fast constraint response and slower release/short edge hold where appropriate;
do not let smoothing permit penetration. Clear history across owner/equip,
teleport/recenter, tracking epoch, load and long sample gaps.

The existing native firing-position guard is separate. R-103/H-021 describe a
muzzle helper with a camera fallback on an owner-forward obstruction test;
`WeaponAttachment.cpp` currently observes and preserves it. Neither that
fallback nor the upstream ADS guard proves correct firing from a freely moved
VR weapon behind a wall. Record final muzzle, blocker and native spawn result
together and choose an explicit blocked-shot policy.

## The additional alignment value of the source

The repository now supplies the implementation missing from the pasted notes:

- `PushAimLock` (`:791..817`) computes
  `weaponRel = inverse(ikAbs) * weaponAttachmentPose`, then
  `desiredIk = desiredWeapon * inverse(weaponRel)`.
  It captures an internal rig relationship rather than the user's arbitrary
  controller angle. It still assumes that relationship is rigid and valid at
  capture; source comments are not proof across all equip/reload states.
- The render-side observation (`:1063..1067`) retains the extra relationship
  between the weapon bone and the rendered attachment:
  `attOffset = inverse(weaponBone) * attachment`. The next skeleton-side update
  uses that to target the same object (`:666..672`). Do not silently substitute
  the weapon bone for the actual attachment.
- The built-in table supplies per-class aim poses for the stock weapons.
  Those are tuned **camera-relative flat-screen sight poses**. They are useful
  geometry/alignment leads; they are not OpenXR grip profiles, barrel collision
  dimensions or automatic proof of a muzzle-forward axis.
- Its render correction moves the left-hand subtree when within 35 cm of the
  weapon (`:1159..1172`). That policy deliberately couples hands and must not
  be imported as the free-left-controller policy in PreyVR.

Prioritize the alignment contract in
[the equip/alignment report](RE-EQUIP-ALIGNMENT-AND-HUD-SCALE-2026-09-09.md), then
add collision against that stable weapon pose. Preserve the current ADIK owner
and prove final attachment/skin publication ordering before adopting upstream
late bone/attachment edits or previous-frame camera assumptions.

## Suggested validation

Use a thin doorframe, flat wall, corner, floor and moving prop. Cover sideways
entry, rotation-only entry, high-speed controller motion, initial overlap,
head-only motion with a fixed controller, and weapon switches. Include a clear
positive control and a known obstructed case. Record desired/constrained pose,
hit/filter data, equip/tracking generation, reach clamp and actual muzzle.

Check that the reticle and native spawn use the intended constrained solution,
the free hand remains independent, and release does not flicker at geometry
edges. Validate the scene query and response separately before wearer comfort.
No new runtime or synthetic collision test was performed in this review.

Upstream is MIT licensed; its license and attribution are retained with the
source snapshot. No upstream source was incorporated into the shipping mod.
