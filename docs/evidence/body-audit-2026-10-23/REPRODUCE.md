# Full-body audit reproduction

Reviewed source: `origin/jordi/robustness-body`, commit `16a69ed3d9fa98803ce9562953122335bcd699d9`.
Integration comparison: `b298f97bc62d902a715fbbd25871ef2cbb8b96bd`.
No Prey process, injection, graphics capture or headset session is used.

The adapters in crouch.cpp and torso.cpp include the unchanged production .cpp
files. Stub definitions replace native/XR boundary functions. geometry.cpp uses
the production pure solver. Fixtures directly construct valid joint locations,
actor stance memory and tracked samples; they do not establish native ABI or
how frequently these transitions occur in the game. Exit zero means the probe
ran, not that the tested invariants passed. Read probe-output.txt.

Use VS 18 2026 / MSVC 19.50.35729.0, x64 Release, static CRT. PowerShell:

```powershell
$cmake = 'C:/Program Files/CMake/bin/cmake.exe'
$review = 'C:/Users/meise/.codex/worktrees/jordi-review/PreyVR'
$built = 'D:/Dev Debug/PreyVR/build/review-body-20261023'
$evidence = 'D:/Dev Debug/PreyVR/docs/evidence/body-audit-2026-10-23'
$probes = 'D:/Dev Debug/PreyVR/build/body-audit-probe-20261023'
& $cmake -S $review -B $built -G 'Visual Studio 18 2026' -A x64 -DPREYVR_BUILD_TESTS=ON -DPREYVR_BUILD_SMOKE_DLL=ON -DPREYVR_BUILD_TOOLS=ON -DPREYVR_BUILD_OPENXR_LOADER=ON '-DFETCHCONTENT_SOURCE_DIR_MINHOOK=D:/Dev Debug/PreyVR/build/headless/_deps/minhook-src' '-DFETCHCONTENT_SOURCE_DIR_OPENXR=D:/Dev Debug/PreyVR/build/headless/_deps/openxr-src'
if ($LASTEXITCODE) { throw 'Configure failed' }
& $cmake --build $built --config Release --parallel 4
if ($LASTEXITCODE) { throw 'Build failed' }
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir $built -C Release --output-on-failure
if ($LASTEXITCODE) { throw 'CTest failed' }
& $cmake -S $evidence -B $probes -G 'Visual Studio 18 2026' -A x64 "-DREVIEW_SOURCE=$review" "-DREVIEW_BUILD=$built" '-DMINHOOK_SOURCE=D:/Dev Debug/PreyVR/build/headless/_deps/minhook-src' "-DMINHOOK_LIB=$built/_deps/minhook-build/Release/minhook.x64.lib"
if ($LASTEXITCODE) { throw 'Probe configure failed' }
& $cmake --build $probes --config Release --parallel 3
if ($LASTEXITCODE) { throw 'Probe build failed' }
foreach ($name in 'crouch','geometry','torso') {
  & "$probes/Release/body_${name}_probe.exe"
  if ($LASTEXITCODE) { throw "Probe failed: $name" }
}
```

The recorded build used the same pinned MinHook library from the previous review
build (`build/review-jordi-20261023`); rebuilding that pinned dependency in the
body build as shown is equivalent. OpenXR 1.1.60 source commit:
`64f2b37c8c6da3d83c9b4d11865ba1fb752cb8ec`; MinHook:
`c3fcafdc10146beb5919319d0683e44e3c30d537`.

Controls and variables:

- Crouch: learn standing eye, acquire crouch on actor A and observe compensation;
  substitute actor B without clearing production state; then feed 120 stable
  standing samples at a different valid eye height. Separately acquire before
  learning a standing baseline. Question: is ownership scoped and relearnable?
- Torso: warm the production estimator, rotate both raw head and room yaw by
  90 degrees as a coordinate rebase; compare retained estimator to Reset control.
  Also query a fresh timestamp-only cache. Question: does published yaw certify
  owner/epoch/reference identity? The fixture is a rebase, not a normal head turn
  or proof that an ordinary recenter changes OpenXR coordinates this way.
- Feet: initialise valid legs, change owner/epoch and translate the whole body
  and anchor by 0.9 m without stick/vertical movement. Compare unchanged planter
  to Reset control. Measure distance between shin-transformed foot joint and
  separately foot-transformed joint. Question: does a valid plan close the leg
  after a discontinuity below the one-metre automatic reset threshold?

Initial probe setup needed additional link stubs and the static CRT; those
setup failures are not evidence of production defects. Frozen output is from
the final successfully built executables. Source excerpts carry original line
numbers. merge-preview.txt comes from non-mutating `git merge-tree --write-tree
--name-only origin/collab/integration origin/jordi/robustness-body` (exit 1 means
four expected conflicts; it does not mean a merge was applied).
