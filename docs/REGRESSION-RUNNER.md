# PreyVR visual regression runner

The runner drives an **already running, explicitly selected Prey/xr-sim session**.
It never launches Prey, injects a DLL, selects a machine-wide runtime or starts a
background service. Default invocation only prints a plan and writes no commands.

**This implementation was validated offline only. No game or xr-sim session was
run for it.** The example fixture is a template, not a measured save/geometry
fixture. The first native run remains an acceptance step.

## Plan safely

From the repository root:

```powershell
.\tools\Invoke-PreyVRRegression.ps1
```

Use `-Scenarios menu,inventory` to select a subset. Python 3.10+ is needed;
`-Python 'C:/path/to/python.exe'` explicitly selects a verified interpreter.
The Python entry point is `tools/regression/runner.py` and also defaults to plan
mode. There is no launch mode.

## Scenarios and evidence

| Scenario | Drives | Checks and captures |
| --- | --- | --- |
| `menu` | Open native pause; right-stick down/up; B closes | Fresh native pause owner, navigation dispatch and return to gameplay; before navigation, after down/up, closed |
| `inventory` | Left X opens PDA; right grip selects next tab; B closes | Native PDA visibility, separate inventory layer frames advancing, tab dispatch; before open, open panel, after tab, closed |
| `two_hand` | Move free left hand to current weapon's native foregrip hint; squeeze/release | Actual mod input hand independence, near/held state, settled blend; before squeeze, attached, detached. Toggle mode gets a second deliberate squeeze |
| `zero_g` | Grip fixed wall; physical pull; still release; physical release push; menu cancellation | Ordinary native state 9, acquisition and native submissions, stable anchor before movement, camera displacement with stationary tracked head, one push authorization, no push on still release or menu cancellation; six captures |

An all-scenario run produces **17 capture sets**, each with left/right PNGs, a
side-by-side PNG and xr-sim's JSON sidecar. `report.html` displays the phases;
`report.json` records observations, commands, checks, cleanup and build identity.

Menu/tab dispatch does **not** prove which row/tab was selected: compare the
images. Grip state does not establish rendered hand alignment. Camera movement
does not establish exhaustive collision behavior or perceived comfort.

## Prepare a native run later

Use the existing launcher/injection/startup tools when runtime testing is
authorized. Build/load the updated DLL: an older DLL has no `regression.state`
reply and will fail preflight. Run Prey under **x64 xr-sim with D3D11**, active
VR input, free pacing and a FOCUSED session. Do not point this driver at VDXR or
a real headset session.

Reserve the game/session for this driver. It takes a cooperative lock in the
explicit xr-sim state directory; other tools must respect that ownership too.
It refuses a busy native command file, a stale/non-Prey runtime PID, a changed
process start identity, stalled frames or a mismatched supported native/DLL hash.
Native replies must echo a unique request token and the same process ID. A
push needs an advancing acknowledged **release serial**; held-motion submission
counts cannot stand in for delivery of that release.

Start in gameplay, not a menu. For the complete suite, prepare an ordinary
zero-g save with a supported long weapon, no active combat and nearby fixed geometry. Ground
menu/inventory/two-hand tests can be selected separately. The runner does not
load saves, select weapons, teleport the player or create a wall.

Copy `tools/regression/default-fixture.json` to a local fixture. Set the raw XR
head/hand poses for that setup. Start xr-sim with its identity origin; do not
apply a simulator-level `recenter` transform before this fixture. The runner
verifies that the mod's located grip/head positions match the commanded rig and
refuses a different coordinate origin. It records the selected reference space;
it does not guess a world-to-client transform. For `zeroG.leftAtWall`, the free left grip must
be within 10 cm of a known fixed surface; `stroke` must point **into** the wall
in raw XR coordinates. Its default is 8 cm over 200 ms. The native query must
actually acquire the surface: a supplied coordinate alone earns no pass.
Reaching a new wall and measuring this fixture need a later authorized run.

Two-hand testing derives a bounded raw-XR placement hint from the current
weapon's existing support region. It still requires actual near/held state and
settled solver output afterward. It cannot fabricate missing rig ownership.

When authorized, the attach-only command has this form:

```powershell
$session='C:/Users/you/AppData/Local/PreyVR/runs/run-EXPLICIT-SESSION'
.\tools\Invoke-PreyVRRegression.ps1 -Execute `
  -RunDir "$session/log" -StateDir "$session/xrsim" `
  -Dll './build/regression-runner-20261026/Release/PreyVR.dll' `
  -Fixture './build/my-measured-fixture.json' `
  -Out './build/regression/baseline-01' -Label baseline
```

`RunDir` must be the actual directory containing `commands.txt`, `results.txt`
and `PreyVR.log`; the driver deliberately does not choose the newest session.
Use a new output directory for every run. The expected DLL hash is checked
against the startup log and the live diagnostic module path. The driver stores
its own source hashes as well.

The harness temporarily fixes body-follow policy to isolate scripted head/hand
motion and opts into only the features it exercises. It neutralizes inputs and
restores those runtime preferences with readback, including after failures.
Surface push authority is disabled **before** clearing a moving grip. If that
cannot be confirmed, cleanup reports a failure and avoids an unsafe release.
No saved VR preference file is edited.

Simulated poses remain at their test positions, and real game time and player
movement are not rolled back. Reload the same save and reproduce the same
starting view before comparing builds. A terminated driver can leave its
cooperative lock; inspect its `runnerPid` and remove the lock only after proving
that owner is gone. The runner never kills a process to reclaim a session.

## Before / after build comparison

Run a baseline before changing a feature. Repeat the same fixture/scenario list
with the candidate DLL, a new output directory and:

```powershell
  -Baseline './build/regression/baseline-01/report.json' -Label candidate
```

`comparison.html` places the baseline and candidate images beside one another
for each eye. Verified baseline PNGs are copied into the candidate output, so
the review folder can be moved/shared intact. `comparison.json` records hashes
and missing captures. A failed candidate can still get a partial comparison.

The comparison refuses incompatible fixture, target, environment, reported
optics/profile or image dimensions, and checks baseline artifact hashes. It
doesn't treat different pixels as a defect or identical pixels as correctness.
Dynamic world state still affects images, which is why reproducible saves and
human/agent review matter.

Every capture requires fresh frame/capture counters, the unique tag, valid PNGs,
matching dimensions and nonempty eyes. Native observations bracket the capture's
XR display time; menu, tracking, reference, player/weapon and interaction ownership
must remain compatible across that interval. This scopes a capture to a scenario
phase; it does not prove the game rendered exactly the submitted camera matrices.
The metadata uses existing publications, not new engine reads or native writes.

Automated success is named **`CHECKS_PASSED_REVIEW_PENDING`**, never headset or
visual acceptance. Missing prerequisites are `BLOCKED`, failed assertions are
`FAILED`, and failed restoration overrides success with `FAILED_CLEANUP`.
The CLI exits nonzero on blocking/failure; zero means automated checks finished
while visual review remains pending. A baseline comparison error is also a failure.

## Offline validation

```powershell
& 'C:/path/to/python.exe' -E -B tests/RegressionRunnerTests.py
```

The tests substitute native/runtime boundaries and exercise the real runner,
capture validation and reports. The real PowerShell trajectory/file protocol is
tested against a temporary fake state reader, including a comma-decimal locale,
atomic release and rejected acknowledgements. Native-channel creation, nonce
matching and refusal to overwrite pending commands are also exercised against
temporary files. No game/runtime call is made.

The C++ `regression_probe` target tests the actual diagnostic serializer against
publication stubs. `two_handed_aim` includes an independently expected translated,
90-degree basis inversion for the placement hint. The complete CTest suite also
registers `regression_runner` when Python 3.10+ is found. These remain offline
evidence; Windows process preflight, real game navigation, actual captures and
the native wall fixture await their own execution.
