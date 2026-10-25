# Zero-g surface-grab evidence — 26 October 2026

Static native contracts and offline fixture evidence, **not a Prey run**.
See [the implementation and next test](../../ZERO-G-SURFACE-GRAB-2026-10-26.md).

- `identity.json`: source and supported target identity, tested Release DLL hash.
- `native-contract.json`: read-only Steam witnesses, runtime gate correspondence,
  corruption controls and explicit limits.
- `configure.txt`, `build.txt`, `ctest.txt`: final build and 85-test offline baseline.
- `flight-options.png`: production GDI painter, Flight page; not a headset image.
- `raster.txt`: production raster resource checks.
- `graph.txt`: code-graph refresh output and any parsing/coverage limits.

Reproduce from the project root in profile-free PowerShell, using the local
supported Steam file. The verifier uses only the Python standard library and
the existing project's read-only PE helpers. It does not load the game DLL.

```powershell
& 'C:/Users/meise/AppData/Local/Programs/Python/Python312/python.exe' -E -B `
  tools/re/verify_surface_grab.py --out build/surface-grab-native.json

& 'C:/Program Files/CMake/bin/cmake.exe' -S . -B build/surface-grab-repro `
  -G 'Visual Studio 18 2026' -A x64 `
  '-DFETCHCONTENT_SOURCE_DIR_MINHOOK=D:/Dev Debug/PreyVR/build/headless/_deps/minhook-src' `
  '-DFETCHCONTENT_SOURCE_DIR_OPENXR=D:/Dev Debug/PreyVR/build/headless/_deps/openxr-src' `
  -DPREYVR_BUILD_TESTS=ON -DPREYVR_BUILD_SMOKE_DLL=ON `
  -DPREYVR_BUILD_TOOLS=ON -DPREYVR_BUILD_OPENXR_LOADER=ON
if ($LASTEXITCODE) { throw 'Configure failed' }
$env:MSBUILDDISABLENODEREUSE='1'
& 'C:/Program Files/CMake/bin/cmake.exe' --build build/surface-grab-repro `
  --config Release --parallel 5 -- /nr:false
if ($LASTEXITCODE) { throw 'Build failed' }
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir build/surface-grab-repro `
  -C Release --output-on-failure
if ($LASTEXITCODE) { throw 'Offline test failed' }
```

`surface_grab_runtime` compiles the actual production command adapter with
explicitly substituted tracking, scene-query and native-owner boundaries.
`surface_grab_transaction` executes the actual production 12-byte scoped request
patch against a poisoned fixture, with normal return and an injected SEH.
These establish harness behavior, not a functioning native hook, real collision
response, exhaustive geometry coverage or headset comfort.
