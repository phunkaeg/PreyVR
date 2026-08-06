# Prey D3D11 device/swapchain creation map

## Scope

After the shared Ghidra database received a full auto-analysis pass, resolve R-001's open question —
which code consumes the DXGI/D3D11 dynamic-loader string cluster — and map the device, adapter, and
swapchain creation path. Read-only static analysis; no process attached and no annotation saved.

## Identity

- Engine target: `PreyDll.dll`, SHA-256 `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`
- Ghidra program: `/Prey/PreyDll.dll`, image base `0x180000000`

## Analysis-pass delta

| Metric | Before | After |
| --- | ---: | ---: |
| Functions | 79,306 | 86,434 |
| Symbols | 80,393 | 557,719 |
| Data types | 42 | 1,114 |
| Memory blocks | 8 | 9 |

Defined strings and xrefs now work for engine literals, which is what unblocked R-001.

### Notes from this pass

1. **Mid-pass readings are unreliable.** An intermediate sample of this database reported 124,871
   symbols and only partial string definition, which produced a since-withdrawn claim that string
   coverage stayed incomplete. Take metrics only after the pass finishes.
2. **String and byte search now agree.** With the index complete, `search_strings` returns 8
   stereo-bearing literals that reconcile 1:1 with the 10 raw byte hits from the earlier exhaustive
   pass, and the absence queries return zero from both methods. The H-001 stereo negative is
   corroborated by two independent methods.
3. **Two prior annotations were lost and have been restored.** `BeginRendererScene`
   (`0x180F7D710`) and `EndRendererScene` (`0x180F7E210`) reverted to `FUN_` names while all 24
   `Ark*`/weapon/camera annotations survived. Both were re-applied with full evidence plate
   comments and the program was saved.
4. **Renderer RTTI is absent.** `CRenderView` and the renderer classes never surface by name, so
   the multi-view question needs structural analysis rather than symbol lookup.

## R-001 resolved

`CreateDXGIFactory1` at `0x181D93408` now has exactly two code references:

| Reference site | Containing function |
| --- | --- |
| `0x180F50057` | `FUN_180F50000` (RVA `0xF50000`) — device/swapchain creation |
| `0x180D87761` | `FUN_180D87710` (RVA `0xD87710`) — second consumer, not yet classified |

R-001's registry note "no code xref yet. Do **not** hook or patch." can be updated: the consumer is
identified. It remains an anchor, not a hook target.

## Creation sequence in `FUN_180F50000`

Verified by decompilation. All COM vtable slot arithmetic was checked against the SDK layouts.

```text
LoadLibraryA("dxgi.dll") -> GetProcAddress("CreateDXGIFactory1")
  -> CreateDXGIFactory1(IID at 0x181D933F8, &factory)      factory stored at param[0]

loop: factory->EnumAdapters1(index)                        vtable +0x60 = slot 12
      until DXGI_ERROR_NOT_FOUND (0x887A0002)

  vendor path A: FUN_180F621A0   when param[0x58] == 1
  vendor path B: FUN_180F623B0   when param[0x58] == 2
  fallback:      LoadLibraryA("d3d11.dll") -> D3D11CreateDevice(
                     adapter, DriverType=UNKNOWN, 0, flags,
                     &featureLevel=0xB000, count=1, SDKVersion=7,
                     &device, &featureLevelOut, &context)

device->CheckFeatureSupport(...)                           vtable +0x108 = slot 33
output->FindClosestMatchingMode(...)                       vtable +0x48  = slot 9
factory->CreateSwapChain(device, desc, &swapchain)         vtable +0x50  = slot 10
factory->MakeWindowAssociation(hwnd, 3)                    vtable +0x40  = slot 8
```

`D3D_FEATURE_LEVEL_11_0` is `0xB000`; `SDKVersion 7` is `D3D11_SDK_VERSION`. The
`MakeWindowAssociation` flags value `3` is `DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER`.

The two vendor paths correspond to the vendor-extension device creation entry points; the strings
`AmdD3D11CreateDeviceExt` and `AmdD3D11CreateDeviceAndSwapChainExt` are present at `0x181FCD998`
and `0x181FCD9B0`, and `nvapi64.dll` is loaded at runtime per the runtime baseline. Which selector
value maps to which vendor is not yet established and should not be assumed.

## Device-manager object layout

Provisional field map for the `param_1` object, derived from how each slot is used:

| Offset | Type | Evidence |
| ---: | --- | --- |
| `[0]` | `IDXGIFactory1*` | receives `CreateDXGIFactory1` output; `EnumAdapters1`/`CreateSwapChain`/`MakeWindowAssociation` called on it |
| `[1]` | `IDXGIAdapter1*` | enumerated adapter |
| `[2]` | `IDXGIOutput*` | `FindClosestMatchingMode` |
| `[3]` | `ID3D11Device*` | `D3D11CreateDevice` output; `CheckFeatureSupport` |
| `[4]` | `ID3D11DeviceContext*` | `D3D11CreateDevice` context output |
| `[5]` | `IDXGISwapChain*` | post-`QueryInterface` on the created swapchain |
| `+0x254` | `UINT` creation flags | `0x80`, or `0x88` when `DAT_182B1C844` is set |
| `[0x45]` | `HWND` | passed to `MakeWindowAssociation` |
| `[0x58]` | `int` vendor-extension selector | `1` and `2` choose the two vendor paths |

Flags `0x80` is `D3D11_CREATE_DEVICE_PREVENT_ALTERING_LAYER_DEFAULTS_FROM_REGISTRY`; the `0x88`
variant adds `D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS`.

This object is distinct from the R-005 renderer singleton, whose own swapchain/device fields at
`+0xAE88`/`+0xAF28` were confirmed again in the same pass.

## Present-side fields from `EndRendererScene`

Decompiling `0x180F7E210` re-confirmed R-006 and R-007 by slot arithmetic — `swapchain` vtable
`+0x40` is slot 8 `Present`, `device` vtable `+0x138` is slot 39 `GetDeviceRemovedReason` — and
exposed the Present argument producers:

| Field | Role |
| --- | --- |
| `renderer+0xB1EC` | non-zero selects the vsync-enabled branch of `FUN_180F780E0` |
| `renderer+0xB1C0`, `+0xB1C4` | the other two `FUN_180F780E0` inputs that produce `SyncInterval` |
| `renderer+0xB1F0` | Present `Flags` argument |
| `renderer+0xAEF8` | Begin/EndScene nesting counter guarding the diagnostic |
| `renderer+0x499C` | frame-slot index; per-slot stride is `0x328` bytes from `+0x4A08` |

No per-eye or per-view iteration exists anywhere in `EndRendererScene`. The only doubled structure
is a 2-entry ring at `+0xAC30` with its counter at `+0xAC28`, which is render-thread command
double-buffering, not stereo.

## Why this matters for the OpenXR lane

`XR_KHR_D3D11_enable` requires the application's device to live on the adapter whose LUID
`xrGetD3D11GraphicsRequirementsKHR` reports. Prey selects its adapter inside the `EnumAdapters1`
loop in `FUN_180F50000`, so that loop is the seam where adapter agreement would have to be enforced
if the active runtime ever demands a non-default adapter. `renderer+0xB1EC`/`+0xB1F0` are likewise
the fields governing desktop Present pacing once a compositor owns frame timing.

Neither is proposed as a hook yet. Both are recorded so the X0/X1 work does not have to rediscover
them, and both need live validation before promotion.

## Safety

Read-only. No process attached, no memory written, no Ghidra annotation created or saved, and the
installed game was not modified.
