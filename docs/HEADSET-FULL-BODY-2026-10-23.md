# Full-body headset test

Connect Quest 3 through Virtual Desktop, start Steam, close any other fleet game,
then use the **PreyVR - Full Body Test** desktop shortcut. It opens the fresh
`build/packages/PreyVR-full-body-test-20261023/Start Prey VR.cmd` package.
The package uses this PC's selected OpenXR runtime (currently VDXR). It starts
at 2016 × 2160 per eye and preserves your saved 3D-view choice (the default is
ALTERNATE EYES). For this test, select **VIEW → 3D VIEW → FULL STEREO** to render
both eyes each frame. This is a local resolution profile; other headsets and
resolution choices remain supported.

After loading a save, stand normally for a moment, then reset with **F12** or
**both grips + left Y**. Hold the left menu button for VR options.

1. Look down, turn your head, lean and squat. Check that the torso and knees
   follow, the feet stay attached, and the world does not move with the body.
2. Crouch about 30 cm, then stand. Check for double camera lowering or a lingering
   height offset. Recenter once while crouched, then stand again.
3. Try snap turning and a small room-scale step. Watch for feet snapping or
   stretching. Keep away from real furniture; the native floor-edge limitation
   is still open.
4. Change weapons, holster/unholster, use a two-hand grip, then open/close
   inventory. Hands and weapon aim should remain aligned.
5. In **VIEW**, toggle **Body Posing** off/on, **Physical Crouch** off/on, and
   verify their choices persist. Body Posing OFF leaves controller arms active.
   Optional: switch movement to BODY RELATIVE and look aside while walking.
6. Pause, reset the view, load a save, and repeat a crouch. Check for unintended
   crouch inputs or stale height/foot offsets after the transition.

If frame rate is too low, close Prey and reduce Width/Height together in the
package's `PreyVR.json` to **1344 × 1440**. Keep ResolutionDefaultsVersion at 2.
Both resolutions retain the same headset-oriented aspect ratio. You can also
compare FULL STEREO and ALTERNATE EYES in VIEW; alternate eyes may shimmer in
motion. This test has not been accepted in a headset yet.

Diagnostics: use the package's `Tune Prey VR.cmd` with `body.report`, `body.pose`,
`pose.report` or `vr.status`. Fresh logs go under `%LOCALAPPDATA%/PreyVR/runs`.
The body and feet research switches are `body.pose 0|1` and `body.feet 0|1`;
use the VR options row for a persisted Body Posing preference.
