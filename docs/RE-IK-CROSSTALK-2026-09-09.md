# IK crosstalk: the shared anchor is an aim-ray point

**There is a concrete feedback path before shared yaw needs to move.**
`GameplayPoseFrame.nativeEye` is copied from the native cached reticle-ray
origin. That field is an unprojected screen point, not the camera's position.
The reticle-follow lane changes its input. Both IK hands then use its output
as their common head anchor.

There is also a separate shoulder dependency: reach compression at 65% includes
35% of the current animated shoulder position in the goal. Zero clamping does
not exclude this dependency. The runtime contribution of either path remains
to be isolated; neither the handover's shared-yaw theory nor an upstream native
animation effect is ruled out by this static investigation.

## Identity and scope

- Source reviewed: `087d3be09dfb209cf9cc260062192ce009241864`.
- Native target: Steam `PreyDll.dll`, x64 little endian, Windows ABI,
  image base `0x180000000`, SHA-256
  `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`.
- Ghidra `/Prey/PreyDll.dll` import SHA matches the disk file. All 2230 bytes of
  the examined ray producer match Ghidra memory and the installed PE image.
- Tools: read-only Ghidra decompile/disassembly/memory/options; Python 3.12
  `pefile` for RVA/file comparison; MSVC 19.50.35729.0 for the standalone math
  harness. Ghidra inline scripts were disabled; program options supplied the
  import identity instead. No security setting was changed.
- The wearer observations refer to the handover's earlier 800768-byte mod DLL
  in `run-20260909-134810`. They are retained as reported headset observations,
  not observations made by this audit.
- No live process interaction or shipping-behavior change was made. Misleading
  anchor comments were corrected in `AimTakeover.h` and `AnimIk.h`. Other-agent
  startup edits and the previous performance audit are separate work.

## 1. Reticle -> native ray origin -> both hands

The complete dependency is present in the current source and target binary:

1. `AimTakeover.cpp:233` builds the right-controller ray; `:319` publishes it.
2. `UpdateAimReticleForRender` (`:373`) uses that sample when `aim.enable` is on.
3. `ReticleFollow.cpp:245` writes projected viewport fractions into the whole
   `ArkPlayer` object at `+0x17EC/+0x17F0`.
4. Native `ArkPlayer::UpdateCachedReticleViewPosAndDir`, RVA **`0x1585320`**, reads
   those fractions (unless the native alternate `+0xA10` branch is selected),
   maps them through viewport information, and unprojects through the view
   camera. Its near-plane world point is written to `+0x17D4/+0x17D8/+0x17DC`.
5. `AimTakeover.cpp:135` copies that point into `frame.nativeEye` after the native
   producer returns, then publishes the frame **before** the mod's aim edits.
6. `AnimIkTakeover.cpp:358` computes each hand as
   `nativeEye + Rz(yaw) * ToEngine(grip - head)`.

Thus a fresh, coherent native-producer snapshot can still contain an
aim-dependent point. Copying before the mod's cached-ray write prevents direct
same-call aliasing; it does not remove this feedback through reticle coordinates.

### Native proof details

Receiver is the whole `ArkPlayer*` in RCX, saved to RDI at VA `0x181585332`.
This is not a secondary-interface displacement. Instructions at
`0x18158536C..0x1815853B6` select `+0xA10` or `+0x17EC`; the selected X/Y values
are multiplied by viewport dimensions at `0x181585435/0x181585444`.

The camera is obtained through `ISystem` virtual `+0x388` at
`0x1815854C7`; existing R-039/H-005C establishes the `GetViewCamera` contract.
The producer reads FOV `CCamera+0x30`, projection ratio `+0x40`, dimensions
`+0x38/+0x3C`, near `+0x4C`, far `+0x64`, and Matrix34 fields. After matrix
construction/inversion, the first homogeneous unprojection uses clip Z=0.
Its screen-dependent result reaches the cached-origin stores at
`0x1815857CF/0x1815857D7`. The second point uses clip Z=1 and is subtracted from
the first stored point, then normalized into `+0x17E0/+0x17E4/+0x17E8`.

The successful-origin stores are **not** a direct copy of the camera translation
column `{+0x0C,+0x1C,+0x2C}`. Failure can also retain an older origin. The existing
`RayOriginEdit` recovery protects against reuse of our own hand/muzzle write;
it does not convert a newly computed native near-plane point into a head anchor.

### Numerical consequence

For a symmetric camera with matched full viewport:

```text
rayOrigin = cameraCentre
          + right   * near * tan(horizontalFov/2) * (2u - 1)
          + forward * near
          + up      * near * tan(verticalFov/2)   * (1 - 2v)
```

Moving `u/v` changes `rayOrigin` with the camera and head completely stationary.
The IK formula adds that entire translation to each raw hand goal.

An illustrative fixed-input fixture uses 120-degree horizontal FOV, near=0.1 m,
2688:2880 aspect, and reticle X moving 0.25 -> 0.75. The native unprojection
formula changes the anchor by **0.173205 m** laterally. The actual committed
`ControllerWorldFromHead` code carries that displacement into a fixed left-hand
goal. At reach=65 with a fixed shoulder, **0.112583 m** remains. Neither goal
clamps in the fixture. These are synthetic values, not the measured displacement
or near-plane value in the wearer's session.

Disabling `aim.enable` also stops `UpdateAimReticleForRender`. Therefore its
effect on the symptom does not uniquely implicate camera yaw: it also interrupts
this feedback path. In origin mode 0, pure right-hand translation with rigorously
fixed orientation should not move the controller-direction reticle by itself;
separate orientation and translation sweeps to test applicability to the exact
reported motion. The source path is not an assertion that every kind of right
controller motion necessarily traverses it.

## 2. Reach compression reintroduces animated-shoulder movement

`AnimIkTakeover.cpp:390` reads the current upper-arm absolute joint as `shoulder`.
It then applies `ScaleReach` before checking the reach sphere:

```text
finalGoal = shoulder + k * (rawGoal - shoulder)
          = k * rawGoal + (1-k) * shoulder
```

Consequently, while unclamped:

```text
delta(finalGoal) = k * delta(rawGoal) + (1-k) * delta(shoulder)
```

At k=0.65, 20 cm of shoulder movement contributes 7 cm of hand-goal movement
even when the raw controller goal is stationary. The compiled fixture confirms
this using the real `ScaleReach` and `ClampToReach`, without clamping.

The wearer test `ik.hands 2` rules out a necessary write to our right ADIK target.
It does not rule out native aim/animation moving the left shoulder or a shared
root before our left-hand write. The animation job performs animation/FK before
ADIK; native downstream processing exists after it too (H-021). Which native
parts actually move during the symptom has not been measured here.

The model-to-world transform alone is not automatically a fault. A third harness
control varies its translation, rotation and scale while holding world-space
goal and shoulder constant: `WorldToModel` followed by `ModelToWorld` cancels it.
The moving *world shoulder*, a later transform mismatch, and a bad shared world
anchor are distinct candidates.

## 3. Corrections to the handover's proposed diagnosis

- The aim lane writes the cached ray, not camera orientation directly. Detached
  aim is intended to leave view direction independent. Camera yaw contamination
  remains testable, but it is not established merely by enabling aim.
- `camYawMdeg`, `headYawMdeg`, `playYawMdeg` and `eyeMm` already exist in `report`.
  Here `eyeMm` reports the field misnamed `nativeEye`; its name is not semantic
  proof. `rpPlayYaw`, `rpHeadPos` and `rpRawPos/rpRawQ` also exist in the coherent
  reticle context. Separate latest-value counters are not a single IK-frame trace.
- `ikGoalMm` currently describes only the **right** goal, in model space. It
  cannot show the left-hand symptom or distinguish world-anchor movement from
  a coordinate-frame change.
- `ik.test 0 0 300` adds a model-space offset to the animated wrist. It is useful
  for identifying the correct limb; it is not a fixed absolute/world-space goal.
- The reported calibration/tracking/clamp controls remain important and are not
  disputed. These two dependencies can operate while all those controls are healthy.

## 4. Cheapest runtime discriminator, for the existing runtime owner

Do not launch a second game or attach a debugger to the wearer's session.
Capture a healthy baseline and preserve every changed setting for restoration.
Head and left controller remain still; retain the wrench and the same scene.

1. Keep `aim.enable 1`, IK drive on, and initially keep reach=65. Compare
   **`aim.reticle 1` against `aim.reticle 0`**, then restore it. Use the field-write
   switch, **not `aim.reticledispatch 0`**: stopping the movie dispatch leaves
   `+0x17EC` writes active. Disabling follow leaves the last screen value in place;
   this is a movement-dependency test, not a test that the reticle recentres.
2. Sweep only right orientation first, then translation while keeping orientation
   fixed. Record reticle fractions, cached origin, camera centre/yaw, raw left
   grip, and raw/scaled/clamped left world goal with a common sample identity.
   If reticle motion, cached-origin motion and left raw-goal motion cease while
   the right cached aim direction still follows, the feedback route is isolated.
3. With reticle follow disabled, compare reach=65 versus **100**, keeping the
   hands close enough that clamp deltas stay zero in both cases. Restore 65.
   If left raw world goal stays fixed but only its scaled goal moves, compare
   that movement with `(1-k) * delta(worldShoulder)`. This isolates compression.
4. If a stationary written world goal still produces a moving rendered wrist,
   investigate post-ADIK modifiers and the actual render/model transform. Do
   not keep changing upstream yaw after the written world goal has been exonerated.

The important missing diagnostic is one coherent per-hand record from the actual
IK callback: owner/reference/tracking generation and time; head and grips; shared
yaw and anchor; character location; world shoulder; raw, scaled and clamped world
goals; written model target; and clamp/calibration validity. Reading separate
global counters cannot reconstruct this transformation chain reliably.

## 5. Fix design and acceptance boundary

**Separate three origins in the data model:** the cyclops/head world anchor,
the native cached ray origin, and the optional hand/muzzle firing origin.
`GameplayPoseFrame.nativeEye` currently conflates the first two. Keep native-ray
restoration and `aim.origin 0` behavior tied to the native ray field; changing
the IK anchor must not silently change firing/interaction policy.

Publish the head anchor and tracking-to-world transform from the upstream
head/view producer, before eye translation and before reticle-derived state.
Carry the same tracking/reference generation used to build it. Consumers should
refuse a missing/stale anchor rather than fall back to the cached reticle point.
Avoid a blind replacement with the live global render camera: it can carry
half-IPD translation in `CSystem::Render` (H-005C / FAIL-HAND-037). A stable
snapshot must have explicit cyclops/eye provenance and correct head translation.

For reach compression, distinguish the stable body/play-space shoulder used for
mapping controller reach from the current physical shoulder used for reach
clamping. A calibrated stable anchor needs per-hand storage and explicit reset
rules for owner/equip, recenter, tracking epoch and calibration changes. Reusing
the current aim-driven shoulder as that reference preserves the coupling.
Changing this policy changes feel and needs its own wearer comparison.

These are source-supported correction designs, not a patched or headset-accepted
build. The local deliverable is the static producer trace plus independently
controlled numerical reproductions and a focused test/fix handoff. No shipping
DLL was rebuilt or injected by this investigation.

## Artifacts and reproduction

`docs/evidence/ik-crosstalk-2026-09-09/` contains the native producer's full
decompile, assembly, checked bytes/identity, pinned source excerpts, the original
handover, and snapshots of the math implementation used by the harness.

Build its standalone `CMakeLists.txt` into a separate directory and run
`ik_crosstalk_audit.exe`. Exact commands and passing output are in
`verification.txt`. The near-point fixture is analytical reconstruction from
the native projection path; it is **not** native-function emulation. IK math
comes from the compiled, unmodified project sources.
