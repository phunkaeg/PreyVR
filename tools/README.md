# Tools

Place small, reproducible helpers here: log parsers, capture-index generators, byte-signature validators, and Ghidra-export normalizers. Keep generated output in `captures/` or `docs/`, not alongside the tool.

## DXGI method probe

`dxgi_probe/` creates a two-pixel hidden D3D11 swapchain in its own process, prints the system `dxgi.dll` RVAs for `IDXGISwapChain::Present` and `ResizeBuffers`, and immediately releases every resource. This lets a live tracer hook the shared implementation without creating probe resources inside the game process.

## Smoke-log checker

`Test-PreyVRSmokeLog.ps1` validates the latest inert-DLL result and logged DLL SHA-256. For a verified run it infers the expected landmark count from the result and requires that every emitted line matches; `-ExpectedLandmarkCount` and `-ExpectedDllSha256` can pin a particular artifact. CTest also runs it against the isolated host's intentional `unsupported` log.

## Engine-map probe

`preyvr_engine_map_probe` maps an on-disk PE with `SEC_IMAGE_NO_EXECUTE` and validates it through the same compiled C++ landmark table used by the DLL. The build manifest queries `--count`, and the doctor invokes it against `PreyDll.dll`; no second PowerShell signature list exists.

## Detached-ray probe helper

`preyvr_ray_probe` accepts a normalized live direction and bounded Z-up yaw, then prints the rotated direction and exactly twelve little-endian bytes. It uses the same tested pure policy as the wrench and interaction stack-local protocols.
