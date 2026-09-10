# In-world terminal cursor: Steam route and earlier-log correction

Vittorio Romeo's [shared notes](https://gist.github.com/vittorioromeo/8985f5c538deccf19e9a8c84fed64aea)
identify `ArkExaminationMode::UpdateReticlePos` for in-world screens on the
Chairloader/EGS build. This is useful for a later terminal-beam integration; it
is separate from the Scaleform hardware-mouse route used by the new floating
main menu and inventory.

On the verified unmodified Steam DLL (SHA-256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`),
the function is RVA **0x15ABC40**, previously identified in the September 8 HUD
research. The donor's **0x157EBA0** must not be used: it is inside an unrelated
Steam function beginning at 0x157EB40.

The Steam update tests examination receiver **+0x98 == 1**, integrates player
**+0x944/+0x948** input into receiver **+0x60/+0x64**, using the supplied float
time step and native speed/projection factors, clamps both coordinates to 0..1,
then dispatches **reticlePosition(x,y)** through the native two-float helper
0x11797C0 on DanielleHUD. The transition at 0x15AB280 writes +0x98 and copies
ordinary player reticle coordinates into this separate cursor state.

The direct call at **0x15850D7** belongs to the player-update region. Its exact
21-byte preceding/call span is:

```text
15850C7  F3 41 0F 10 77 14        movss xmm6, [r15+14h]
15850CD  48 8D 8F B0 09 00 00     lea rcx, [rdi+9B0h]
15850D4  0F 28 CE                 movaps xmm1, xmm6
15850D7  E8 64 6B 02 00           call 15ABC40h
```

The containing ArkPlayer_Update entry is 0x1584FE0. Ghidra's recovered body is
truncated before this span; the PE's chained unwind entries and direct bytes
cover it. Do not treat that incomplete decompiler body as absence of the call.
The receiver adjustment is player+0x9B0, and the time step is in **XMM1**, not an
integer argument. Raw decompiles and this decoded span are preserved in
`evidence/ui-pointer-2026-09-10/examination-steam-proof.json`.

This corrects the R-109 research-log statement that both reticlePosition
producers are examination transitions. One is an input-integrating update
called by the player update path while examination is active. The correction
does **not** establish a competing producer during ordinary weapon aiming:
the native active-examination gate still applies.

The transition's +0x94 field distinguishes native examination types. Its value
1 branch is consistent with the donor's worldUI enum; terminal identity and the
downstream click/UV consumer still need independent Steam proof before a new
write or call is shipped. A screen-space HUD reticle also does not by itself
prove controller-ray intersection with a terminal's physical display.

Next: prove that downstream world-screen consumer, the selected terminal's
geometry/UV mapping and its click/cancel action ownership. Then test a controller
ray against one accessible terminal. No examination hook or field write was
added by this investigation.
