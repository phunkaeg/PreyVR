# Ghidra sync

This file records the reproducible state of the shared analysis database; it is not a substitute for the database itself.

| Date | Program | Module baseline | Action | Result |
| --- | --- | --- | --- | --- |
| 2026-07-30 | `/Prey/Prey.exe` | `F179987F…C55199F1` | Verified existing import | 64-bit launcher, image base `0x140000000`, 110 functions. |
| 2026-07-30 | `/Prey/PreyDll.dll` | `7D6E322F…B05311A7` | Imported with automatic analysis | 64-bit primary engine target, image base `0x180000000`, 79,303 functions. |
| 2026-07-30 | `/Prey/PreyDll.dll` | Same | Read-only renderer reconnaissance | `.rdata+0x1D93408` contains the DXGI/D3D11 dynamic-loader cluster. No code-reference annotation was written. |
| 2026-07-31 | `/Prey/PreyDll.dll` | Same | Live/static renderer correlation | Renamed `0x180F7D710` to `BeginRendererScene` and `0x180F7E210` to `EndRendererScene`; added evidence plate comments. Added instruction comments at `0x180F7E48A` (`renderer+0xAE88` = swapchain, slot 8 Present) and `0x180F7E4DA` (`renderer+0xAF28` = device, slot 39 GetDeviceRemovedReason). |
| 2026-07-31 | `/Prey/reference/PreyVR-PreyDll-EGS-0485c85b.dll` | `0485C85B…99F0A63` | Imported reconstructed Chairloader/PDB reference | Canonical EGS image imported at base `0x180000000`. This is a semantic reference only; its RVAs are not valid for the Steam target. |
| 2026-07-31 | EGS reference and `/Prey/PreyDll.dll` | EGS plus Steam `7D6E322F…B05311A7` | Cross-build camera/aim mapping | Named matching player resolver, movement-state, camera update/custom-view, reticle update/getter, and ArkWeapon reticle/firing functions. Exact bytes, invariant control flow, and live hits were used instead of a global RVA delta. |
| 2026-07-31 | `/Prey/PreyDll.dll` | Same | Live aim/camera annotations | Added evidence plate comments to `ArkPlayerCamera::UpdateView`, both custom-view setters, `ArkPlayer::UpdateCachedReticleViewPosAndDir`, `IArkPlayer::GetReticleViewPositionAndDir`, `ArkPlayerMovementController::GetMovementState`, and three `CArkWeapon` reticle consumers. Saved all open programs. |
| 2026-07-31 | `/Prey/PreyDll.dll` | Same | Wrench A0b static refinement | Renamed Steam functions at `0x1813BD620`/`0x1813BF730` to `ArkWrenchComponent_GetHits`/`ArkWrenchComponent_OnHit`; added evidence plate comments and labels at post-reticle-copy `0x1813BD6F5` and result-ready `0x1813BFAA9`. Static control flow proves A0b can edit only the stack-local direction and capture results before effects. |
| 2026-07-31 | EGS reference and `/Prey/PreyDll.dll` | EGS plus Steam `7D6E322F…B05311A7` | Native interaction mapping | Rebuilt and named Steam function bodies for `ArkPlayer_Update` (`0x181584FE0`), `ArkPlayerInteraction_Update`/`Interact`/`PerformInteraction` (`0x181594E20`, `0x181593690`, `0x181593980`), and target-selector update/candidate/getter functions (`0x18159A630`, `0x18159A660`, `0x18159A0A0`, `0x181599E10`). Added evidence plate comments, the stack-local ray label at `0x18159A70A`, and the ArkPlayer `+0xAC8` dispatch comment at `0x1815850DC`. |
| 2026-07-31 | EGS reference and `/Prey/PreyDll.dll` | Same | Viewmodel attachment lead | Uniquely matched, rebuilt, and named Steam `CArkWeapon_AttachToHand` at `0x1816914F0`; paired decompilation confirms the weapon's `IAttachment*` field at `+0x2B0`. Added an evidence plate comment and left the function outside the runtime gate pending the actual late-frame transform writer. |

| 2026-08-01 | `/Prey/PreyDll.dll` | Same | Full auto-analysis pass | Final state: 86,434 functions, 557,719 symbols, 1,114 data types, 9 memory blocks. Defined strings and xrefs now resolve for engine literals, which unblocked R-001. Readings taken mid-pass are unreliable and were superseded — an intermediate sample showed 124,871 symbols and only partial string definition. **Regression:** the 2026-07-31 `BeginRendererScene` (`0x180F7D710`) and `EndRendererScene` (`0x180F7E210`) function names did not survive the pass; all 24 `Ark*`/weapon/camera function names did. See the corrected characterisation below — an earlier version of this row claimed the plate comments were lost too, which was never verified. **Renderer RTTI is absent**, so `CRenderView` and the renderer classes never surface by name; structural analysis is required for view-count questions. |
| 2026-08-01 | `/Prey/PreyDll.dll` | Same | Re-applied renderer annotations and promoted R-025 | Restored `BeginRendererScene`/`EndRendererScene` names with full evidence plate comments carrying build hash, confidence, and validation recipe. Named `0x180F50000` to `InitializeD3D11DeviceAndSwapChain` (R-025) after decompilation confirmed its factory/adapter/device/swapchain call sequence — a confirmed call relationship, not a string-only association. Saved the program. |

| 2026-08-15 | `/Prey/reference/PreyVR-PreyDll-EGS-0485c85b.dll` | EGS `0485C85B…99F0A63` | Imported PDB-derived types from Chairloader headers | Parsed `Vec3`, `Vec2`, `Quat` and `CRenderCamera` into the EGS reference program's type manager via Ghidra's CParser; 4 types added, 1,118 total. `CRenderCamera` resolves to 72 bytes with `vOrigin` at `+0x24`, `fWL` at `+0x30`, `fNear` at `+0x40`, matching R-026's measured offsets exactly. Source: `tools/Chairloader-src/Common/Prey/CryRenderer/IRenderer.h`. The Steam program was not modified. |

## What a full re-analysis pass actually destroys

Tested directly on 2026-08-07 against the surviving database, because the first characterisation of
the 2026-08-01 loss was a guess and turned out to be wrong.

| Annotation | Address | Batch | Survived? |
| --- | --- | --- | --- |
| EOL comment, swapchain/Present identification | `0x180F7E48A` | renderer, 2026-07-31 | **Yes**, verbatim |
| EOL comment, device/GetDeviceRemovedReason | `0x180F7E4DA` | renderer, 2026-07-31 | **Yes**, verbatim |
| Function name `BeginRendererScene` | `0x180F7D710` | renderer, 2026-07-31 | **No** |
| Function name `EndRendererScene` | `0x180F7E210` | renderer, 2026-07-31 | **No** |
| Function name `ArkWrenchComponent_GetHits` | `0x1813BD620` | wrench, 2026-07-31 | Yes |
| Plate comment on that function | `0x1813BD620` | wrench, 2026-07-31 | Yes |
| User label `A0b_ReticleRayCopied_StackLocal` | `0x1813BD6F5` | wrench, 2026-07-31 | Yes |

**The pattern is not "annotations without other metadata are vulnerable."** That was the original
hypothesis and this table refutes it: the two renderer functions carried EOL comments from the very
same annotation batch, and those comments survived while the names did not.

**The observed pattern is that address-anchored annotations are durable and function names are not.**
Comments and labels bind to an address; a function name is a property of a `Function` object that
re-analysis can destroy and recreate. The pass raised the function count from 79,306 to 86,434, so
function objects were being created in bulk.

**Practical rule.** After any re-analysis, re-verify **function names** specifically. Surviving
comments are not evidence that names survived — they are precisely what makes a database *look*
intact while its symbols are gone.

**Known limit.** Whether the renderer pair's original *plate* comments survived was never checked;
they were overwritten during restoration before anyone thought to look. Only the function-name loss
and the EOL-comment survival are established.

## Annotation policy

1. Search/inspect before writing an annotation.
2. Give every function/data annotation a build hash, confidence, and validation recipe in its comment or the address registry.
3. Do not rename an unknown function after a string-only association. Use a neutral, scoped label only after its call relationship is confirmed.
4. Save the program only after an intentional annotation batch; the 2026-07-31 renderer pair is the first evidence-backed annotation batch.
