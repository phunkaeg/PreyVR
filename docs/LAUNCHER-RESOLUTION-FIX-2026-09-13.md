# v0.5.0 launcher resolution correction

The public launcher introduced at 3e55a657 defaulted to 2560x1440 to preserve
native menu framing, although earlier headset testing used 2688x2880. Normal
VR startup preserves Prey's projection, so a widescreen render can lack the
vertical field requested by the headset. The tester's border report is
consistent with this, but no capture or coverage report from that tester has
yet established the cause on their machine.

## Install the launcher patch

Close Prey. Extract `PreyVR-0.5.0-resolution-fix-20260913.zip` and copy
`Start-PreyVR.ps1` over the same file beside your existing v0.5.0
`Start Prey VR.cmd`. Start the game using that CMD file as usual.
Keep your existing `PreyVR.json` and DLLs. This patch contains no DLL and does
not enable the experimental stereo inventory renderer.

## Behavior

- Fresh configurations use **2016x2160** per eye. This has the same 14:15 aspect
  as the earlier 2688x2880 headset route, at 4,354,560 instead of 7,741,440
  pixels. Compared with the old 2560x1440 default, the pixel count rises about
  18%; performance has not been measured for this patch.
- An unversioned saved **2560x1440** pair migrates once to 2016x2160, with an
  exact backup named `PreyVR.json.before-resolution-fix-<unique-id>.bak`.
- Other saved sizes and all unrelated settings remain intact. The launcher
  records `ResolutionDefaultsVersion: 1` so subsequent launches respect edits.
- An explicit `-Width ... -Height ...` pair wins over saved values. Supplying
  only one dimension fails before launching, rather than mixing aspect ratios.
- To keep 2560x1440 deliberately, add `"ResolutionDefaultsVersion": 1` to the
  existing JSON before launching, or supply both dimensions explicitly.
- For more detail use `Width: 2688`, `Height: 2880`. Native menus may letterbox
  within their floating panel. World view coverage takes priority over filling
  that panel. The native menu renderer and pointer mapping are unchanged.

This corrects the launcher regression, not all possible FOV coverage failures.
It does not query the headset's recommended resolution or enable runtime-frustum
projection. The tester's game FOV, headset/runtime and actual view coverage still
matter. In normal gameplay, `xr.coverage` reporting `leftShort=1` or
`rightShort=1` means the rendered view does not fully cover the runtime request.
The next step for a persistent border is that report, followed by rendering and
validating the runtime-requested projection with matching weapon/HUD geometry.

## Validation

`tests/LauncherResolution.Tests.ps1` passed in Windows PowerShell 5.1 and
PowerShell 7. It executes the launcher's actual configuration functions extracted
from its parsed AST, without executing the game-launch body. Cases cover fresh
defaults, legacy migration, explicit overrides, custom and versioned saved
sizes, partial and invalid dimensions, exact backup preservation, unrelated
settings, and migration idempotence.

Full launcher `-DryRun` also passed in both shells against the supported Steam
binary and existing local player package. Its saved 2688x2880 and UI scale 160
were correctly retained. Dry runs launched nothing and wrote no config.
No new native build or headset test is claimed. Existing DLL work in the dirty
tree is unrelated and excluded from the patch.
