# Native flight static/fixture verification

Run from the project root in profile-free PowerShell. The source implementation
and limits are described in [the flight note](../../NATIVE-ZERO-G-2026-10-25.md).
No new game save, screenshot, xr-tape trace or headset evidence is claimed.

```powershell
$env:MSBUILDDISABLENODEREUSE='1'
& 'C:/Program Files/CMake/bin/cmake.exe' -S . -B build/native-flight-20261025 `
  -G 'Visual Studio 18 2026' -A x64 `
  -DPREYVR_BUILD_TESTS=ON -DPREYVR_BUILD_SMOKE_DLL=ON `
  -DPREYVR_BUILD_TOOLS=ON -DPREYVR_BUILD_OPENXR_LOADER=ON `
  '-DFETCHCONTENT_SOURCE_DIR_MINHOOK=D:/Dev Debug/PreyVR/build/headless/_deps/minhook-src' `
  '-DFETCHCONTENT_SOURCE_DIR_OPENXR=D:/Dev Debug/PreyVR/build/headless/_deps/openxr-src'
& 'C:/Program Files/CMake/bin/cmake.exe' --build build/native-flight-20261025 `
  --config Release --parallel 5 -- /nr:false
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir build/native-flight-20261025 `
  -C Release --output-on-failure
& 'C:/Users/meise/AppData/Local/Programs/Python/Python312/python.exe' -E -B `
  tools/re/verify_native_flight.py `
  --deps 'D:/Dev Debug/PreyVR-integration/build/re-tools' `
  --out build/native-flight-contract.json
& build/native-flight-20261025/Release/preyvr_vr_options_panel_tests.exe `
  build/native-flight-options
```

Check each native exit code. The two added CTest targets are `native_flight`
(pure state machine, delivery acknowledgements and 3D quaternion conversion) and
`flight_runtime` (the actual production adapter with native/publication
boundaries replaced). The existing VR-options test also applies the new setting
through its actual high-bit mailbox and saves/loads it.

The verifier checks Steam file identity, the state producer and concrete
dispatch, constructor/vtable/callee, input-subobject and setter/consumer flow,
numerical vertical signs and both shipped profile variants. It compares the
actual C++ runtime-gate arrays to target bytes. Corrupting the concrete callee or
the source field gate must fail. It emits derived metadata only; archives and
full XML/decompiler exports remain local and ignored.

`identity.json` pins the source and built DLL. `configure.txt`, `build.txt` and
`ctest.txt` preserve the actual final build/suite, and `world-page.png` is the
production GDI menu painter. `native-contract.json` contains the static witnesses.
`graph-refresh.txt` records an incremental code-only graph refresh with existing
semantic nodes retained; it is a navigation aid, not native evidence.

The native bridge itself is not called on fake memory by the fixtures. Its
field/ABI contract is covered by target bytes; executing the actual hook,
native input acceptance, flight direction, comfort and performance require
the next authorised game/headset run. Surface interaction is deliberately later.
