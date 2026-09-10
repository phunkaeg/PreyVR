# Right-stick click: Favorites Wheel

Candidate 10 adds an OpenXR boolean `weapon_wheel`, bound to the right thumbstick
click on Touch and Index. The coherent controller publication carries its state
to the main-thread interaction lane, slot 4, default native `mouse3` (0x102).
The existing edge handler sends press and release, retries refused release, and
releases on input loss or disable. It blocks presses in recognized menus and
requires release before a button held through a menu can act again. Stick axes
are unchanged. No new native function, hook or object offset is introduced.

The native key identity is target proof: Steam CMouse::Init RVA 0x9D45C0 calls
MapSymbol with 0x102 and literal mouse3. The existing decompile receipt is
`evidence/ui-pointer-2026-09-10/native-input-mode-proof.json`.
Bethesda's PC manual documents middle-mouse as Favorites Wheel:
https://assets.ctfassets.net/rporu91m20dc/53M5yrNdu8aowEssaesGcm/390ff006505cb92ae1105c9530f950f9/PREY_pc_minimanual-EN-04digital.pdf
This establishes the default shortcut, not acceptance by a customized live action map.

The EGS symbol export supplies a structural lead only: ArkFocusModeComponent::ProcessInput
starts the UI for mouse_enter_focusmode / enter_focusmode. ArkFocusModeUIComponent::Show
uses the HUD element and wheelItemPromptsOpen / wheelFadeIn, not a distinct menu
IUIElement. Do not add an invented DanielleFocusMode element or claim existing
modal beam selection covers this wheel. No EGS address is compiled into this change.

Validation: release build and current offline suite are captured as build-10.txt
and ctest-10.txt in the same evidence folder. No game launch or injection was
performed for this change. Live opening, selection and dismissal remain to check.

## Preserved headset result from candidate 09

The user confirmed stereo was fixed and the in-game popup menu was curved.
Candidate 09 DLL SHA256: 546a76766c9054f2c896bc2ee683e9306404f6a606d1015d4904eef69206766f.
The session subsequently crashed at Steam PreyDll RVA 0x27EC40 on
JobSystem_Worker_4(Regular), reading 0x23C. Cause unproven. Logs and dump are under
`evidence/ui-pointer-2026-09-10/crash-headset09/`. Candidate 10 preserves the early
rendered-eye dequeue fix; it is not a crash fix or a stable-release claim.
