# Changes made to the installed game

**This project's standing rule is that the installed game is never modified.**
Everything the mod does is in-process and reversible on exit. This file is the
exception list, and it exists so that "the game install is untouched" never has
to be assumed. If it is empty apart from history, nothing has been changed.

Anything here is an **explicit owner decision**, not a mod requirement. The mod
neither makes nor depends on any of it, and it must keep working when every
change below is reverted.

## 2026-09-08 — intro videos disabled by renaming

Owner's decision, owner's action, for the obvious reason: the startup videos are
long and every test cycle plays them again.

Four files in `D:\SteamLibrary\steamapps\common\Prey\GameSDK\Videos` were renamed
from `.bk2` to `.bak`. Renamed, not deleted, so restoring is a rename back.

| File (stem) | Bytes | SHA-256 |
| --- | --- | --- |
| `Ryzen_Bumper` | 3,519,800 | `59F7343B5EA4B4F1FFE7A733D11ECCB2E5FC51E2045523B299E15F99B39F534C` |
| `ArkaneLogoAnim_Redux_1080p2997_ST-16LUFS` | 7,141,344 | `F468331B6B64C271BD256E5B83F0E914E96E959BE7903987EC0CCDA4059F21F2` |
| `Bethesda_logo_anim_white` | 8,724,684 | `57C07A612739541EDDEC4847D61E69C8EFD12F49442B4699B9582331C68F9CA3` |
| `LegalScreens` | 4,718,544 | `5D0AADA41A0A3060762D36EE8955CCB31DA12B1336B0E08C2C5726990E6D8ADB` |

The hashes are of the **renamed** files, which are byte-identical to the
originals: only the directory entry changed. They are recorded so a later restore
can be verified rather than assumed, and so a corrupted or replaced file is
distinguishable from a renamed one.

**`LegalScreens_Console.bk2` was deliberately left alone.** It is a separate
console-platform file with a similar name, and renaming it was not intended.

### Restoring

```powershell
./tools/Restore-PreyIntroVideos.ps1            # what it would do
./tools/Restore-PreyIntroVideos.ps1 -Apply     # do it
```

Steam's *Verify integrity of game files* also restores them, but it re-downloads
whatever it considers missing and touches the whole install, so prefer the
script. Verification is the fallback if a file is ever lost rather than renamed.

### What this does and does not affect

* **It does not weaken the mod's safety gate.** The fail-closed landmark check
  covers `PreyDll.dll` (SHA-256
  `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`) and 34
  exact byte signatures inside it. No video file is part of that check, and no
  executable byte changed, so the gate means exactly what it meant before.
* **It does not affect any recorded evidence.** Nothing in the address registry,
  the hypotheses or the failure registry depends on startup video playback.
* **A game update will restore them**, silently, because Steam replaces missing
  files. If the intro videos reappear after an update, this is why, and the
  rename simply needs repeating.
* **It changes what a fresh launch looks like.** Anyone comparing an old capture
  or log against a new one should know the startup sequence is shorter now, so a
  timing difference at launch is expected rather than a regression.
