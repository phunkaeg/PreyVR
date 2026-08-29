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

## OpenXR adapter probe

`preyvr_xr_adapter_probe` answers the one Hurdle 2 question a running Prey cannot: which GPU the
OpenXR runtime requires, and what index that is in the order R-025's `EnumAdapters1` loop walks --
which is exactly the value `r_overrideDXGIAdapter` (R-052) takes. It creates a real `XrInstance`,
calls `xrGetD3D11GraphicsRequirementsKHR`, and matches the returned LUID against the DXGI
enumeration, ending in one actionable line:

```
preyvr_xr_adapter result=matched enum_index=N r_overrideDXGIAdapter=N override_needed=yes|no
```

It runs **out of process** -- no Prey, no injection, no writes -- and is the first code here that
actually calls into OpenXR rather than inspecting its files. It needs a headset connected and the
runtime streaming to reach the LUID; without one it stops at `XR_ERROR_FORM_FACTOR_UNAVAILABLE` and
still prints the adapter list, which is half the answer and costs nothing.

It tries `XR_CURRENT_API_VERSION` first and falls back to `XR_API_VERSION_1_0`, printing which was
accepted. That is not defensive padding -- see F-010, where the pinned 1.1 SDK is rejected outright by
a 1.0 runtime.
