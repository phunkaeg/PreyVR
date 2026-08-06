# Prey EndScene / Present cadence capture

- Date: 2026-07-31 (Australia/Sydney)
- Scenario: Prey loaded into a saved game and actively rendering gameplay
- Game module: `PreyDll.dll`
- Game SHA-256: `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`
- Capture mechanism: Cheat Engine non-breaking hardware execute breakpoints
- Mutation: none

## Anchors

| Anchor | Session address | Stable form |
| --- | ---: | --- |
| `IDXGISwapChain::Present` | `0x7FFEF5B3DAD0` | local `dxgi.dll+0xDAD0` |
| `EndRendererScene` | `0x7FFE326AE210` | `PreyDll.dll+0xF7E210` |
| EndScene command dispatch | `0x7FFE32719D21` | `PreyDll.dll+0xFE9D21` |
| Renderer singleton pointer | `0x7FFE3426E8E0` | `PreyDll.dll+0x2B3E8E0` |

## Results

- Present registers showed a stable swapchain in `RCX`, `RDX=1` (`SyncInterval`), and `R8=0` (flags).
- The EndScene dispatch loaded the singleton object, read its vtable, and called slot `0x8D0`; the live slot target was `PreyDll.dll+0xF7E210`.
- A simultaneous nominal one-second count recorded 109 Present entries and 103 EndScene entries. The total tool interval including setup/readback was 1,162 ms.
- `EndRendererScene` directly references the game diagnostic `EndScene without BeginScene`.
- ReGenny could inspect the module-resident singleton but Microsoft RTTI lookup returned no typename.
- Scanning only the renderer object's first `0xC000` bytes found the exact live swapchain pointer at `+0xAE88` and `+0xAFA8`. The primary field is loaded by `PreyDll.dll+0xF7E48A` immediately before the slot 8 Present call.
- `renderer+0xAF28` held a D3D11-owned COM object. `PreyDll.dll+0xF7E4DA` calls its vtable slot 39 in the device-loss branch, confirming `ID3D11Device*` / `GetDeviceRemovedReason`. The same device pointer was mirrored at `+0xAF98`.

## Cleanup and limitations

Both count breakpoints and the earlier register-capture breakpoint were removed successfully. CE reported no pre-existing breakpoints before the capture. The count difference is not treated as proof of multiple swapchains or a precise frame rate; breakpoint setup timing and wrapper/overlay behavior remain possible explanations.
