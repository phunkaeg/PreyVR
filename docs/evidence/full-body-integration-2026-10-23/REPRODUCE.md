# Integrated body fix verification

Source: `43ddbf15ebbc30b76322a82f917a179cd4b62916` on `collab/integration`.
Merge: `9b0d9c16f88eb7f4959758211ba3c6cd5d69cbed`.
Separate body fix: `55bc75b7e5d8199aacc77c683737e6c3611f82e1`.

Windows x64 Release, VS 18 2026 / MSVC 19.50.35729.0, pinned MinHook
`c3fcafdc10146beb5919319d0683e44e3c30d537` and OpenXR 1.1.60
`64f2b37c8c6da3d83c9b4d11865ba1fb752cb8ec`.

Configure the checked-out source with CMake and the normal DLL/test/tool options:

```powershell
$cmake = 'C:/Program Files/CMake/bin/cmake.exe'
$built = 'D:/Dev Debug/PreyVR/build/full-body-integration-20261023'
& $cmake -S 'D:/Dev Debug/PreyVR' -B $built -G 'Visual Studio 18 2026' -A x64 -DPREYVR_BUILD_TESTS=ON -DPREYVR_BUILD_SMOKE_DLL=ON -DPREYVR_BUILD_TOOLS=ON -DPREYVR_BUILD_OPENXR_LOADER=ON '-DFETCHCONTENT_SOURCE_DIR_MINHOOK=D:/Dev Debug/PreyVR/build/headless/_deps/minhook-src' '-DFETCHCONTENT_SOURCE_DIR_OPENXR=D:/Dev Debug/PreyVR/build/headless/_deps/openxr-src'
if ($LASTEXITCODE) { throw 'Configure failed' }
& $cmake --build $built --config Release --parallel 5 -- /nr:false
if ($LASTEXITCODE) { throw 'Build failed' }
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir $built -C Release --output-on-failure
if ($LASTEXITCODE) { throw 'CTest failed' }
```

The recorded full integration run passes 76/76 tests in 20.03 seconds. The
launcher wording was then corrected without changing C++ or the DLL; its stereo
contract test and the actual packaged launcher under Windows PowerShell 5.1
`-DryRun` were checked after that script change.

Discriminating regression checks compile actual production adapters:

- BodyFollowRuntimeTests.cpp includes BodyFollow.cpp, substitutes native/XR
  boundaries and supplies stance memory and fresh tracked frames. It asserts
  baseline-before-acquisition, successful crouch/lift, rejection of stale or
  blocked requests, replacement player, epoch/reference changes, disabled VR and
  disabled physical crouch. An acknowledged crouch on the same native player is
  the recenter positive control: compensation survives, old input does not.
- BodyPoseRuntimeTests.cpp includes AnimIkTakeover.cpp, compares retained vs
  reset torso estimates after coordinate rebasing, refuses mismatched consumer
  contexts, and exercises the actual no-frame callback invalidation. PlanBody
  assertions cover discontinuities, owner changes, long gaps and option changes.
- BodyPoseTests.cpp checks bone lengths and shin/foot attachment for unreachable,
  axial and coincident targets; degenerate legs must refuse.
- VrOptionsRuntimeTests.cpp drives the real options adapter through high-bit
  VIEW edits while preserving WORLD choices, saved OFF and launch override rules.
- Existing carry/grenade, FOV WARP, native wrist, UI and renderer contracts remain
  in the combined suite. A passing test does not establish native acceptance.

Production menu raster test writes all pages; inspected PNGs retain VIEW and
WORLD. Labels fit beside their values; GDI ownership returns to its baseline.

The player package uses the actual Steam target and verified DLL/loader/injector.
Its config seeds 2016x2160 but preserves the selected runtime and saved VR options.
DualStereo=1 permits the saved 3D VIEW choice; it does not force FULL STEREO over
alternate or mono. The checklist tells the tester how to select FULL STEREO.
The desktop shortcut points to this package. `-DryRun` validates local paths and
the supported PreyDll hash; it does not create a session or launch the game.

No game, injection, xr-sim gameplay session or headset run occurred. Native
crouch acceptance, camera/body feel, shadow consistency, floor edges, performance
and stereo quality remain headset/game acceptance items. Previous failing
measurements are preserved in the separate body-audit evidence directory.
