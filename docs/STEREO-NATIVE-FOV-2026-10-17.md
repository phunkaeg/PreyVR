# The double image on Meta's runtime: declare the native field, place the picture inside it

Branch `jordi/stereo-fusion` (fix, `f1b29cf`) and the local test integration
`jordi/stereo-dual-test` (with PR #5 merged, `3fe447d`). 2026-10-17.
Code: `include/preyvr/NativeFovPlacement.h`, `src/common/NativeFovPlacement.cpp`
(tests: `tests/NativeFovPlacementTests.cpp`), `XrSessionHost.cpp` (`PlaceEye`), `VrMode.cpp` (3D VIEW).

## Symptom

Quest 3 over Air Link, Meta's runtime, `jordi/polishing` @ `2d246ea`, 2026-10-16: the wearer saw a double image
everywhere, near and far, standing still. The log showed alternate-eye stereo working as designed
(`stereo_armed ipd=0.062`, contracts accepted for both eyes). The mock fused perfectly in every condition tried:
still, head sway, strafing, 72 Hz pacing, 2016x2160, menu transitions, rotated and pitched heads.

## Cause

**Meta's PC runtime does not honour a projection view's declared field.** Its view configuration reports
`fovMutable = false`, and its compositor lays every projection view across the eye's own native field whatever the
layer declares. Others found the same: [Khronos forum, "Oculus runtime ignores Projection Layer View's
pose"](https://community.khronos.org/t/oculus-runtime-ignores-projection-layer-views-pose/110078), "the FOV is not
mutable on Oculus". The user's own Dishonored 2 mod had found it on a Quest 2: "advertising a custom symmetric fov did
not help; the compositor maps by the native fov".

This mod declared Prey's symmetric frustum (about +-49 x +-51 degrees at 2016x2160). The Quest 3's eye fields are
mirrored-asymmetric (left eye about -52/+42 horizontally). Laid across the native field, each eye's image shifts by
about 10 degrees, in opposite directions, so the eyes have to diverge about 20 degrees to fuse it. That is impossible,
and it happens at every distance. SteamVR (PR #5's Steam Frame sessions) and the mock honour the declared field, which
is why neither showed it.

## Fix

Every eye now **declares `views[eye].fov`, the field the runtime asked for**. Prey's pixels are placed inside it at
the angles they were rendered for. Both frusta are linear in tangent, so the placement is a pure offset at the
render's own density: a `CopySubresourceRegion` into the swapchain slice with no resampling and no loss of sharpness,
with the rect as the projection view's `imageRect`. Parts of the native field the render does not reach are cleared
to black. Declaring the native field is correct on every runtime, not only on immutable ones.

- Stereo pairs, PR #5's both-eyes pairs and the flat mirror all go through `PlaceEye`. The flat picture is declared
  at one centre pose for both eyes.
- `fovMutable` is read at session creation and logged: `view_configuration ... fovMutable=0`.
- If the native field does not fit the swapchain at the render's density, the slice falls back to the old
  declaration and logs `native_fov_placement reason=does_not_fit_or_degenerate`. That happens only for flat frames
  with Prey's narrow menu camera, which are not shown, because the menu panel replaces the projection layer.
- `xr.nativefov 0|1` (default 1) is the A/B switch.

## Mono and the 3D VIEW option

`3D VIEW` (VR options, last tab VIEW; `vr.stereo 0|1|2`; `vr-options.ini` `stereo`):

| value | mode | what it does |
| --- | --- | --- |
| 2 (default) | FULL STEREO | PR #5: both eyes from every frame |
| 1 | ALTERNATE EYES | one eye per frame, the earlier path |
| 0 | MONO | one picture from the centre of the head in both eyes under one pose (the Dishonored mod's mono): no depth, fuses at any frame rate, each eye refreshed every frame |

Mono reuses the stereo machinery, so every frame keeps its render-time pose (the compositor's timewarp stays
correct). The eye is built with no offset (`CameraEditHook`), and `SubmitStereoPair` gives the frame to both eyes.
Switching follows the order that never publishes a both-eyes contract without its second submission.
`PREYVR_DUAL_STEREO=0` (launcher `DualStereo 0`) still forces alternate eyes for a launch, and the launcher now also
reports `dual_stereo=mono`.

## Tools

- **Mock: Meta's behaviour.** `DVR_MockHmd.txt` takes a 7th field, `fovMutable`
  (`Launch-Mock.ps1 -FovMutable 0`, the default). With 0, `xrGetViewConfigurationProperties` reports false and the
  CPU compositor lays each view across the eye's native field. This reproduced the headset fault headless.
- **Mock: display pacing.** `DVR_MockPace.txt` = Hz makes `xrWaitFrame` block on vsync slots like a real runtime.
- **`stereofusion.py`** (jordi-mock harness) judges whether a pair of eye images fuses. It resamples both onto one
  tangent grid, matches patches (normalised correlation, left-right consistency) and reports vertical disparity,
  divergence and implied depth. A self-test covers correct, swapped, vertical-offset and mono pairs. It reads mock
  captures and OculusMirror screenshots (`MirrorShot.ps1`).
- **`xr.pattern 1`** replaces Prey's pixels with a stereo test pattern drawn through each eye's declared pose and
  field: crosses at 0.6 m, 2 m and 30 m, rulers at 3 m, and scattered dots at 10 m. It splits submission faults from
  render faults, and a wearer can read it directly. **`xr.swap 0|1`** exposes the existing eye swap.

## Measured (mock, 2016x2160, 72 Hz pacing; disparity in degrees)

| runtime | mode | vertical p95 | divergent | result |
| --- | --- | --- | --- | --- |
| immutable (Meta) | alternate, old declaration | 1.7 | 76-100% (-19 deg) | double at every distance |
| immutable | pattern, old declaration | 0.015 | 99.8% (-19.7) | everything at 0.37 m |
| immutable | alternate, native field | 0.035 | 0% | fuses, wall 1.6-2.3 m |
| immutable | pattern, native field | 0.014 | 0% | 0.60 / 9.2 / 27.7 m (true 0.6 / 10 / 30) |
| immutable | full stereo (PR #5), native field | 0.034 | 0% | fuses |
| immutable | full stereo, head 35 deg + sway | 0.134 | 0% | fuses |
| immutable | mono | 0.015 | 0% | zero disparity |
| mutable (SteamVR-like) | full stereo, native field | 0.035 | 0% | fuses (no regression) |

The pattern's far distances read about 8% short. Integer placement offsets cost up to half a pixel per eye
(0.03 deg), which is far below fusion limits.

Cost: placement adds nothing measurable. In one session, full stereo ran at 33.8 ms per frame against 36.4 ms with
the old declaration, which is noise. Full stereo costs about twice alternate eyes or mono at 2016x2160 on an RTX 3060
Laptop (34 against 18 ms in that session; 20 against 14 in an earlier one).

## Open

- **A black band about 3 degrees wide at each eye's outer edge.** Prey's horizontal field (the FOV slider, `cl_hfov`
  98 on the test profile) is narrower than the Quest 3's outer 52 degrees. A slider of about 104 covers it. A render
  of 2160x2016 instead of 2016x2160 would cover it with no vertical waste. Neither is automatic yet.
- **Render size against frame rate.** Meta recommended 1232x1360 per eye in the 2026-10-16 session, and the default
  render is 2016x2160, which is 2.6x the pixels. Full stereo at a size near the recommendation may hold 72 Hz on this
  laptop. That has to be measured in the headset.
- **Headset confirmation**: OculusMirror captures (`MirrorShot.ps1`) and the wearer, with the pattern and each mode.
