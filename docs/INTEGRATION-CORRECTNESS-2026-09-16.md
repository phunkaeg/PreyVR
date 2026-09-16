# Integration correctness batch — 16 September 2026

## Result

The separate `codex/integration` worktree preserves our development baseline and
implements the first three review fixes. Release builds; 41/41 tests pass.
Native xr-sim run `run-20260916-185943`, PID 116452, produced 7,414 projection
frames with zero xrEndFrame errors. The process was closed after disabling VR.
No release, remote push, main merge or donor-source import was performed.

## Changes

- A rendered eye now carries its source head pose, source display time, eye
  displacement, reference generation and actual camera frustum together.
  Submission no longer labels completed pixels with the latest xrLocateViews pose.
- Missing/invalid metadata rejects the world layer. A recenter invalidates both
  held eye images; the first fresh pair must use the new reference. The flat
  diagnostic projection also refuses a missing native FOV instead of substituting
  the runtime FOV. Menu composition panels do not require world camera metadata.
- The handoff retains FIFO order and no longer skips metadata independently of
  pixels. Refused camera builds carry invalid placeholders; overflow refuses until
  reset. Its mutex protects only the small fixed-size record copy/reset, never
  rendering, engine calls or file I/O.
- Startup's motion-blur and AA commands complete only after GetCVar/GetIVal
  readback matches. A mismatch/missing value times out. Dedicated completion
  counters prevent unrelated console commands from completing a startup setting.

The native call route is R-052: Steam RVA F50A1A calls console vtable +B8 and
F50A2F tail-calls ICVar +10. Fresh offline disassembly agreed with the earlier
live capture. Getter addresses must be executable inside PreyDll; SEH rejects
invalid pointers. Only the two fixed renderer settings use this verification API.

## Native results and controls

Both `r_MotionBlur 0` and `r_AntialiasingMode 1` logged `verified_readback` before
VR reached active. Captured left/right images show Talos Lobby and the weapon.
The baseline used Quest 3 simulator optics, 63 mm IPD, 1344x1440 source images.

Head control changed from position (0,1.6,0), identity orientation to
(0.15,1.7,-0.1), yaw 25 degrees, pitch 10 degrees. The retained left-eye pose was
(0.121451,1.7,-0.0866875), quaternion
(0.0850898,0.215616,-0.018864,0.972581), consistent with the rotated eye offset.
Six trace samples retain submission poses different from that frame's newest
locate. This is evidence that the completed eye images are no longer retimed by
submission; it is not independent proof of all GPU draw ownership.

The final diagnostic report, taken before shutdown, recorded 5,426 accepted eye
records and two refusals. Those refusals occurred at recenter, rejecting queued
old-reference records; subsequent projection submission recovered. The legacy
`fovDiverged` counter currently includes whole-contract refusals, not just FOV
numeric mismatches. The complete trace ran longer than that report.

Inventory input was attempted but did not establish a verified inventory
transition in this run. Do not count it as inventory acceptance.

## Failed control and correction

The initial candidate refused all gameplay: it compared SViewParams translation
against the final CSystem world camera. Native diagnostics showed -0.037496 in
the player-camera producer versus 314.963531 in the world camera. Player world
translation is added downstream. The corrected guard checks the full orientation,
finite final position, reference generation and successful unit-scale positional
application; it retains the original tracked position rather than comparing local
and world positions directly. Both failed-run logs are preserved.

The first queue candidate also poisoned on mutex contention; that was removed
because contention alone does not establish a lost frame. Overflow still refuses.
The first capture timeout coincided with loading. A later startup did not render
until the game window was restored/focused; it then activated successfully.
The xr-sim shot helper also waited for an extra capture sequence after the capture
had already completed. Captures were obtained through acknowledged `shot <name>`
commands and their actual image/sidecar files instead.

## Limits and reproducibility

Evidence is in [the evidence directory](evidence/integration-contract-20260916/).
The manifest and trace summary distinguish the native-tested DLL
`65BEAFB45BD76963A98BD9356AF3AD24E389B7C0DBB4C4D48201C4A64EB3697F`
from the final build hash. After the native run, review added rejection when
UpdateFrustum is unavailable and made verified-command failure reporting immune
to unrelated console result updates. The final build passed all 41 tests;
those final failure-path changes have not been re-exercised in a native run.

The existing alternate-eye render ordering assumption remains; a queue alone does
not prove engine/GPU frame identity in every render mode. Rotation-only/non-unit
position-scale research paths lack the new production contract and refuse world
submission. This work retains scalar-IPD parallel eye rendering; it does not add
canted-eye support. It is not headset acceptance or feature parity with Jordi.

Build: `cmake --build build/integration --config Release -j 8`.
Tests: `ctest --test-dir build/integration -C Release --output-on-failure`.
Next: headset validation, then the comfort and scoped gameplay work in the
[integration plan](INTEGRATION-PLAN-2026-09-16.md).
