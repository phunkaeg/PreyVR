# Runtime baseline

## 2026-07-31 session

The first live observation confirmed these relevant modules in the Prey process:

| Module | Observed size | Meaning |
| --- | ---: | --- |
| `Prey.exe` | 581,632 bytes mapped | Launcher/host process. |
| `PreyDll.dll` | 48,353,280 bytes mapped | Primary game/engine implementation. |
| `dxgi.dll` | 1,302,528 bytes mapped | Swapchain/presentation runtime. |
| `d3d11.dll` | 2,490,368 bytes mapped | Active Direct3D 11 runtime. |
| `D3DCOMPILER_47.dll` | 4,210,688 bytes mapped | Shipping shader compiler/reflection dependency. |
| `nvapi64.dll` | 5,681,152 bytes mapped | NVIDIA driver API. |

No `openxr_loader.dll` was loaded. Module bases and PIDs are deliberately omitted because ASLR makes them session-specific.

## Local DXGI method resolution

The reproducible out-of-process helper `preyvr_dxgi_probe` created and destroyed a hidden two-pixel D3D11 swapchain. On this installed Windows `dxgi.dll` it reported:

| Method | System-module RVA |
| --- | ---: |
| `IDXGISwapChain::Present` | `dxgi.dll+0xDAD0` |
| `IDXGISwapChain::ResizeBuffers` | `dxgi.dll+0x388C0` |

These RVAs are trace anchors for the current Windows build only. Re-run the helper after a Windows/DXGI update. The non-breaking logging trace below used the locally resolved Present entry; shipping code must use the game-owned interface rather than a system-module RVA.

## Live frame-boundary correlation

The safe CE trace resolved the first Prey frame boundary without installing code:

| Observation | Result |
| --- | --- |
| Swapchain | Stable pointer for the sampled session |
| Present arguments | `SyncInterval=1`, flags `0` |
| Engine return address present on raw stack | `PreyDll.dll+0xFE9D27` |
| Dispatch | `PreyDll.dll+0xFE9D21` calls renderer vtable slot `0x8D0` |
| Resolved slot target | `PreyDll.dll+0xF7E210` (`EndRendererScene`) |
| One-second simultaneous count | 103 `EndRendererScene`, 109 `Present` |

The small count difference may include timing at breakpoint installation/removal or an additional presentation path. It does not justify assuming a strict one-to-one contract. The near-equal cadence, call-stack relationship, and internal diagnostic together make `EndRendererScene` a high-confidence engine boundary.

## Game-owned D3D11 objects

Read-only singleton inspection resolved the graphics objects used directly by `EndRendererScene`:

| Renderer field | Live type evidence | Use |
| --- | --- | --- |
| `+0xAE88` | DXGI-owned vtable; exact pointer passed as Present `RCX`; slot 8 call in game code | `IDXGISwapChain*` |
| `+0xAF28` | D3D11-owned vtable; slot 39 call on the DXGI failure path | `ID3D11Device*` / `GetDeviceRemovedReason` |
| `+0xAF40` | D3D11-owned interface adjacent to the device's immediate-context state | Provisional; do not bind by offset yet |

Mirror pointers were observed at `+0xAFA8` (swapchain) and `+0xAF98` (device). The implementation should use the primary evidenced fields and call `ID3D11Device::GetImmediateContext`; it should not encode the provisional context-related layout.

## Live camera and aim ownership

PDB-translated Steam functions were sampled with single hardware execute breakpoints in the same loaded-save session:

| Boundary | Live observation |
| --- | --- |
| `ArkPlayerCamera::UpdateView`, RVA `0x148B820` | Hit immediately. `RCX` was exactly the captured ArkPlayer pointer plus `0x12A0`; `RDX` pointed to plausible `SViewParams` position, quaternion, near-plane, and FoV values. |
| `ArkPlayer::UpdateCachedReticleViewPosAndDir`, RVA `0x1585320` | Hit immediately with the ArkPlayer base in `RCX`. A reversible reticle-X change altered the generated world direction while execution/camera state was frozen. |
| `IArkPlayer::GetReticleViewPositionAndDir`, RVA `0x157CBB0` | Hit immediately. `RCX` was ArkPlayer plus the expected `IArkPlayer` subobject offset `0x40`. The return address mapped to a native HUD-marker consumer; static searches found additional aim-assist, psi, wrench, and weapon consumers. |

All temporary bytes were restored and verified. Hardware, software, and memory breakpoint lists were empty afterward, and the debuggee was running.
