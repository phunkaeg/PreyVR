# Regression runner — offline validation, 26 October 2026

These are **offline/source/harness** records. No Prey launch, injection,
xr-sim session, real gameplay capture or headset test was performed.

- `identity.json`: exact source commit and tested development DLL hash; scope
  and observation limits.
- `configure.txt`, `build.txt`, `ctest.txt`: Release configuration/build and
  **87/87 offline CTest targets**.
- `focused.txt`: final runner/probe/two-hand/surface-adapter checks.
- `runner-tests.txt`: **17 Python cases**, including the actual PowerShell
  file/trajectory code against temporary fake state and command responses.
- `plan.json`: actual default PowerShell invocation, plan-only and no IPC.
- `probe-fixture.json`: actual C++ diagnostic serializer run with publication
  stubs; its module path belongs to the test executable, not a game session.
- `graph.txt`: code graph refresh, including three pre-existing partial-parse
  warnings. The current stager covers C++/PowerShell but excludes Python;
  the runner was reviewed/tested directly. The staged new probe and surface
  adapter matched the final C++ source bytes.

Run instructions and the full boundary are in
[REGRESSION-RUNNER.md](../../REGRESSION-RUNNER.md).

The native process/loaded-module preflight, real command acceptance, actual eye
capture, support-grip rendering and a measured zero-g wall fixture still need
the first authorized runtime execution. Passing harness tests does not promote
those claims. A future successful native run remains pending visual review.
