# Offline VR scheme integration replay

These executables include the production `MoveLane.cpp`, `AimTakeover.cpp`, and
`HudBridge.cpp`. External game/input boundaries are stubbed. They do not launch,
attach to, inject into, or post input to Prey. The movement hook fixture checks
all five arguments and the native return; the native byte verifier independently
establishes why that signature is required.

From the repository root, with MSVC 2026 and the existing MinHook checkout at
`build/headless/_deps/minhook-src`:

```powershell
cmake -S tools/re/vr_scheme_review -B build/vr-scheme-review -G 'Visual Studio 18 2026' -A x64
cmake --build build/vr-scheme-review --config Release
ctest --test-dir build/vr-scheme-review -C Release --output-on-failure
python -B tools/re/verify_vr_scheme_native.py
```

This desktop session exposed duplicate `PATH`/`Path` environment entries to
MSBuild. Launching configure/build through Python's normalized environment fixed
that tooling issue without changing machine settings, for example:

```python
import os, subprocess
subprocess.run(["cmake", "--build", "build/vr-scheme-review", "--config", "Release"],
               env=dict(os.environ), check=True)
```

The frame-contract checks model the skipped-copy control flow. They do not mock
the whole OpenXR host or assert compositor behavior. HUD queue tests prove thread
handoff and argument lifetime using a missing-module positive control; they do
not claim Scaleform accepted a movie function. Aim tests assert that the installed
gameplay ray, published sample, and reticle input use the same origin in all six
mode/calibration combinations. They do not test actual movie pixel placement.
