# Viewmodel Tweaks & Ironsights - developer notes

Reverse-engineering notes for `PreyDll.dll` (the EGS 2021-08-18 build Chairloader patches Steam to).
All offsets are RVAs into that DLL. Read alongside `Src/ModMain.cpp`.

## Building from source

Standard Chairloader DLL-mod workflow (Visual Studio 2022 + CMake + vcpkg), see
<https://github.com/thelivingdiamond/Chairloader/wiki/DLL-Modding-%E2%80%90-Mod-Project-Setup>:

```
cmake -S . -B _build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake ^
  -DCHAIRLOADER_COMMON_PATH=<Chairloader repo>/Common ^
  -DMOD_DLL_PATH=<Prey>/Mods/Vee.ViewmodelTweaks
cmake --build _build --config Release
```

`Src/ModMain.cpp` contains the whole mod; `CommonMod/` and `CMake/` are the unmodified
Chairloader mod SDK glue.

## The weapon / camera pipeline

Prey is built on CryEngine 3 and kept Crysis 3's *procedural weapon animation* system
(`CProceduralWeaponAnimation`, `CWeaponOffsetStack`, `CWeaponPoseOffset`, ...). Every frame:

1. `CProceduralWeaponAnimationContext::Update()` calls
   `CProceduralWeaponAnimation::Update(float dt)` (`PreyDll.dll+0x17D31E0` in the
   Chairloader-supported build). That function blends the weapon's authored pose offsets,
   look/strafe sway, recoil, bumps and zoom offsets into two view-space `QuatT`s:
   `m_rightOffset` (`this+0x2D8`, weapon hand) and `m_leftOffset` (`this+0x2F4`, support hand).
2. The context rotates those into the aim frame (`Quat::CreateRotationVDir(aimDir)`) and
   pushes them *additively* onto the right/left hand IK joints of the first-person arms
   through an `IAnimationOperatorQueue` pose modifier.

The mod post-hooks `CProceduralWeaponAnimation::Update` and multiplies the user's offset into
both `QuatT`s (translation in view space: X right, Y forward, Z up; rotation as an
`Ang3(pitch, roll, yaw)` quaternion). The stock `g_debugWeaponOffset 2` designer tool is
left untouched (the hook backs off while it is active).

Crouch detection reads `ArkPlayer::m_stance` (`STANCE_SNEAK`). Aiming is the mod's own key
binding, read through an `IInputEventListener` registered with `gEnv->pInput` (so it is ignored
while any cursor is on screen), with an optional camera zoom pushed into `ArkPlayerZoomManager`
via `SetDesiredHFOV(cl_hfov * factor, time, false, high)` / `ClearDesiredHFOV`.

Additive offsets inherit whatever the body animation does with camera pitch (aim-up/down poses) and
the look-sway, which makes real ironsights impossible. The lock works in two steps, both deterministic
(no prediction filters, no feedback loops):

1. **Skeleton side.** After the context has pushed its additive operators, the mod pushes `eOp_Override`
   position/orientation for the weapon-hand IK joint (`IAnimationOperatorQueue` vtable slots 8/9, joint
   index at `ctx+0x18`): `camera(model space) * aimPose * (weapon relative to IK joint)^-1`. This brings
   hands, weapon bone and all effects attached to it (muzzle flash, projectile origin) to the aim pose.
   The camera is not known yet at this point - Prey evaluates the first-person skeleton first and then
   builds the camera *from* it - so this step uses the previous frame's exact camera plus this frame's
   mouse delta (`ArkPlayerCamera::m_rotation` is updated in `PrePhysicsUpdate`, before the animation).
2. **Render side.** `ArkPlayerCamera::UpdateView` (`+0x145ED50`) is where the camera becomes final:
   `GetDesiredPositionAndRotation` (`+0x145C530`) reads the `#camera` attachment of the arms character
   (`IAttachmentManager::GetInterfaceByName` = vtable +0x38, `IAttachment::GetAttModelRelative` = +0x68),
   rotates it by the entity rotation, and combines the view angles with the lean roll
   (`m_leanAngle * m_leanAmount`, `m_leanAmount` being smoothed at 10/s in `PrePhysicsUpdate`). The
   position is returned *relative to the entity* because the arms use camera-space rendering
   (`ENTITY_SLOT_RENDER_NEAREST`; `CView::Update` adds the entity position). Nothing touches the
   skeleton between this call and rendering, so the mod post-hooks it and writes the weapon's final
   transform directly:
   * The weapon is not rendered from its entity. `CArkWeapon::AttachToHand` binds the weapon's character
     as a *skeleton attachment* (`CSKELAttachment`) of the arms and clears the weapon entity's own render
     flag; it is drawn at `armsWorldMatrix * CAttachmentBONE::m_AttModelRelative`. That QuatT lives at
     `attachment+0x130` and `GetAttModelRelative()` returns a reference to it, so the mod sets it to
     `camera(model) * aimPose` (`CArkWeapon::m_pAttachment` is at `+0x2B0`).
   * The hand joints must stay on the grip, so the final absolute pose of the weapon-hand subtree (and
     the support hand when it is within 35 cm of the weapon) is moved by the same rigid delta through
     the reference `ISkeletonPose::GetAbsJointByID` (slot 24) returns; the skinning reads that buffer at
     render time. Hierarchy comes from `IDefaultSkeleton` (`GetJointCount` slot 1, `GetJointParentIDByID`
     slot 2, `GetJointNameByID` slot 6, `GetJointIDByName` slot 7; the weapon hangs off `r_handProp_jnt`,
     child of the hand joint).
   The delta is normally a few millimetres (the skeleton-side push already did the work); it is what
   used to leak through as bob/lean jitter.

Motion while locked is opt-in: the game's procedural recoil/bump and look/strafe offsets (captured from
`CRecoilOffset/CBumpOffset/CLookOffset/CStrafeOffset::Compute`) can be kept at any fraction, and a
"head-bob coupling" subtracts a fraction of the camera's own fast motion (camera position minus a
low-pass of it) so the weapon lags the head like a real viewmodel. All of it is computed from this
frame's exact data, so it cannot jitter.

The hip pose is captured the same way so the transition blends smoothly. `aimPose` is therefore
"weapon (attachment) relative to camera": x = 0 / no rotation means centered, and it is stored per weapon
class (`IEntityClass::GetName()` of the equipped `CArkWeapon`).

Some mannequin fragments (jump/land on certain weapons) contain no weapon procedural clip, so
`CProceduralWeaponAnimationContext::Update` (`PreyDll.dll+0x17D4D50`) early-outs on
`m_instanceCount < 1` and nothing is applied — the weapon visibly snapped back. The mod hooks that
function and, when enabled, forces the count to 1 for the duration of the call so the (empty) pose
stacks and the user offset still get applied.

For the FOV: the weapon is rendered in the "nearest" pass with its own FOV stored in
`r_DrawNearFoV` (`EFQ_Set/GetDrawNearFov` read/write the same float).
`ArkPlayerZoomManager::Reset()` and `SetNearFOVUnlocked()` hard-code it to **55°**, and
`ArkPlayerZoomManager::Update()` scales it proportionally to the horizontal FOV while zooming
(which is why plain `r_DrawNearFoV = x` in `game.cfg` never sticks). The mod re-applies
`vm_fov * currentHFOV / baseHFOV` every frame after the game's update, and respects the
game's "near FOV locked" state (used when the weapon must share the world FOV).


## Feel layer (sprint pose, aim sway, view drag)

* Pure per-frame state (`UpdateFeel`, from `MainUpdate`) with two outputs: additive view-space offsets for the
  hip path (`ApplyOffset`, faded out by the aim blend) and a weapon-local post-multiplied transform for the aim
  path (`ComputeAimLocal`, applied as `extra * aimPose * local * kick` on both the skeleton and the render side).
* Sprint: `ArkPlayerMovementFSM::IsSprinting()` (`+0x1570580`); speed from `pe_status_living::vel`.
* View drag: turn rate from `ArkPlayerCamera::m_rotation` (yaw = Ang3.z, pitch = Ang3.x), a damped spring
  (sub-stepped at 8 ms) towards the rate; the offset is proportional to the spring state, clamped.
* Aim sway rotates the weapon about its own pivot (post-multiplied), so the sights leave the crosshair while the
  shot still follows the camera - which is what makes it matter with the reticle hidden.

## Fire animation while locked

* The skeleton-side push drives the right IK joint through the game's own `IAnimationOperatorQueue`. With
  `eOp_Override` the fire *animation* (the pistol's kick is animated, the shotgun's is mostly the procedural
  `CWeaponRecoilOffset`) is wiped out. The push is therefore `eOp_Additive` (3, what the game uses for its
  offsets: position added, orientation pre-multiplied in model space): `final = animated (+) add`. Knowing
  `add` of the previous frame gives the animated hand of the previous frame; its deviation from a slow
  reference (frozen while a shot plays) is the kick, fed into the aim pose (weapon-local, per-weapon
  `aim_kick_scale`, gated to the shot window). The one frame of animation velocity the additive lets through
  is corrected exactly by the render-side placement.
* `CArkWeapon::FireWeapon` (`+0x16659B0`) is the shot event for every weapon.

## Reticle and the in-world screen cursor

* `hud_reticleSetting` (SCVars+0x930; 0 off, 1 default, 2 dot) is exactly what the options menu writes
  (`gameOptions.xml`, Action="hud_reticleSetting"). Its only readers are in `CArkUIHUD` (4 sites); at 0 the
  HUD is sent `reticleDisplay("none")` *and* `interactIconDisplay("none")`.
* On in-world screens the cursor is the HUD reticle: `ArkExaminationMode::UpdateReticlePos` (`+0x157EBA0`)
  moves `m_reticlePos` (+0x60) and sends `reticlePosition` to the HUD, so a hidden reticle means no cursor.
  The mod therefore un-hides the reticle while `ArkPlayer::m_examinationMode.m_examinationState != inactive`
  with `m_examinationType == worldUI`, and only ever writes the cvar when its own target value changes.

## Robustness

* Everything read from the game (camera, entity rotation, attachment, bones, procedural offsets) is checked
  for finiteness and near-unit quaternions at the boundary; everything written back (offset, operator push,
  attachment, hand joints) is checked again. Accumulated state (low-passes, springs, blends, the additive chain,
  the kick reference) is reset when a bad number shows up, and persisted settings are sanitized on load.
  `Ang3(Quat)` in CryMath uses an unclamped `asin`, so all Euler conversions go through a clamped copy.
* The additive hand push is only subtracted from the final pose when the skeleton actually applied it (the
  animation update can be skipped while the hook still runs); otherwise the chain would grow without bound.
* Mod cvars are unregistered in `ShutdownSystem`: the console keeps raw pointers to the names and storage,
  which vanish with the DLL, and touches them again at engine shutdown (crash on exit otherwise).

## Three ABI traps

1. Member functions returning a struct larger than 8 bytes (`QuatT`, `std::vector`, `std::pair`) take the
   hidden return pointer AFTER `this`. Never hook or call them through an SDK declaration that returns the
   struct directly; declare `R* (T* _this, R* _ret, args...)`.
2. `PreyFunction` instances are rebased when the DLL initialises; a function-local `static` one is
   constructed too late and calls the raw RVA.
3. **The SDK's `physinterface.h` is stock CryEngine; Prey's physics structs are not.** `pe_status_living`
   is 152 bytes in Prey (an extra `Vec3` after `velGround`; `groundSurfaceIdx` at +0x64, not +0x58), the
   header's is 136. `GetStatus(&living)` on a header-sized local wrote 16 bytes past it - exactly onto the
   caller's saved xmm6 in `UpdateFeel`'s frame, which `MainUpdate` was using for `dt`. The corrupted step
   made every exponential filter downstream (convergence, wall pull-back) either freeze (tiny/negative dt ->
   k = 0) or snap (large dt -> k = 1), and which one depended on the frame layout of each build. No crash,
   no NaN, just wrong smoothing - found by tracing filter value vs. target per update. Rule: every struct the
   *game* fills in (`pe_status_*`, `ray_hit`, ...) is declared with Prey's layout when known and always with
   a generous pad after it (`PreyStatusLiving`, `PaddedRayHit` in ModMain.cpp). Symptom to remember: a
   smoothed value that tracks its target instantly or not at all while the code is provably right means
   the *step* is garbage, and a garbage step in a value that was fine a call earlier means a callee
   trashed a callee-saved register - look for a stack struct the callee writes into.
