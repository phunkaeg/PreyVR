# Tests

This directory is for deterministic tests that do not require a running game or headset: engine signature gating, captured aim fixtures, DLL fail-closed lifecycle, matrix conventions, asymmetric OpenXR FoV projection, eye-pose composition, coordinate conversion, dead zones, and controller-aim calibration.

Runtime validation belongs in `docs/TEST_PLAN.md` with its resulting artifacts recorded under `captures/`.

Run the complete no-game/no-headset loop with:

```powershell
./tools/Run-Headless.ps1
```

The fresh loop configures and builds, executes eleven deterministic CTest targets, replays the captured aim, wrench-query, and interaction-query fixtures, validates the frame-observer plan and OpenXR state/file preflight, optionally verifies the installed Prey module architecture, exact hashes, and all 22 compiled signatures, checks the ReGenny notebook, resolves the local DXGI method table using an out-of-process two-pixel swapchain, and loads the real DLL in an isolated fail-closed host whose identity, unpinned lifecycle, log, and default-off exports are checked automatically.
