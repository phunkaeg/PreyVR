# Reproduce the static and fixture checks

Run from `D:/Dev Debug/PreyVR` in profile-free PowerShell. Dependencies below
already exist locally; these commands do not install or launch anything.

```powershell
$env:MSBUILDDISABLENODEREUSE='1'
& 'C:/Program Files/CMake/bin/cmake.exe' -S . -B build/ui-touch-hacking-20261025 `
  -G 'Visual Studio 18 2026' -A x64 `
  -DPREYVR_BUILD_TESTS=ON -DPREYVR_BUILD_SMOKE_DLL=ON `
  -DPREYVR_BUILD_TOOLS=ON -DPREYVR_BUILD_OPENXR_LOADER=ON `
  '-DFETCHCONTENT_SOURCE_DIR_MINHOOK=D:/Dev Debug/PreyVR/build/headless/_deps/minhook-src' `
  '-DFETCHCONTENT_SOURCE_DIR_OPENXR=D:/Dev Debug/PreyVR/build/headless/_deps/openxr-src'
& 'C:/Program Files/CMake/bin/cmake.exe' --build build/ui-touch-hacking-20261025 `
  --config Release --parallel 5 -- /nr:false
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir build/ui-touch-hacking-20261025 `
  -C Release --output-on-failure
& 'C:/Users/meise/AppData/Local/Programs/Python/Python312/python.exe' -E -B `
  tools/re/verify_hacking_input.py `
  --deps 'D:/Dev Debug/PreyVR-integration/build/re-tools' `
  --out build/hacking-contract-check.json
& build/ui-touch-hacking-20261025/Release/preyvr_vr_options_panel_tests.exe `
  build/ui-input-preview
```

Check every native exit code before proceeding. Final combined build and CTest
output are frozen here; the configure log records exact compiler/generator.
`source.patch` records the production and fixture changes against integration
`00e04ca7d7fd13bb7d84a48fa5a46043809cb10b`. `identity.json` identifies the source
commit, built DLL, supported native module and fetched collaborator branch tips.

The GDI raster is the actual production options painter, inspected at page 10.
The code graph refresh retained existing semantic nodes and emitted 9,924 nodes
and 19,852 links; three pre-existing parse limitations are recorded in its log.
It is a navigation aid, not native proof. Generated graph caches are not shipped.

The native verifier deliberately corrupts a witness byte and mutates the action
map. The actual adapter fixtures replace native input/tracking/HUD boundaries;
their success is not proof that Prey accepts inputs or draws the panel correctly.
No process attachment, game launch, xr-sim run or headset acceptance is claimed.

See [the player acceptance checklist](../../UI-TOUCH-HACKING-2026-10-25.md).
