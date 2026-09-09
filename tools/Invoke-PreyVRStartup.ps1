<#
.SYNOPSIS
    Brings an injected Prey session up to the standard VR configuration, in the
    one order that works.

.DESCRIPTION
    Every setting here has been forgotten, mis-ordered or mis-valued at least once
    in a live headset session, costing wearer time each go. The point of this
    script is that none of it depends on anyone remembering.

    **Order is not cosmetic. Three steps are order-dependent:**

    * `xr.srgb` must precede `xr.start`. The swapchain format is fixed when the
      swapchain is created, so asking afterwards returns 4 and the session keeps
      the wrong gamma until it is restarted. This is the gamma control -- it is
      not a console cvar, and looking for one in the console allowlist wasted a
      headset session.
    * `view.recenter` must precede `view.apply`. Applying without a reference
      returns 2, `no_reference_recenter_first`, and head tracking silently does
      nothing.
    * `xr.native` defaults **off**. FAIL-STR-048: a previous bring-up rebuilt the
      "known-good" config from a settings table and missed this because it was
      named only in a section heading, and the run was lost to it.

    **`r_DrawNearFoV` is VERTICAL, and it moves with the render aspect.** It is
    not a constant, and treating it as one is how it drifted: 88.507 is the
    vertical half of a `120 x 88.507` frustum measured at **16:9**. Rendering at
    2688x2880 makes the aspect 0.933, where that same number yields roughly 84
    degrees horizontal instead of 120, and a wearer sees an over-magnified
    weapon. Confirmed in a headset 2026-09-08: 123.35 looked correct at 0.933.

    So it is derived here from the actual aspect, holding the horizontal field at
    the 120 degrees that was accepted in a headset:

        tan(V/2) = tan(60 deg) / (width / height)

    The buffer trap is separate and still live. The FOV export reads
    `[0, tanLeft, tanRight, tanUp]`; taking indices 2 and 3 as the vertical pair
    mixes a horizontal tangent with a vertical one and yields 104.254, which was
    set live twice before a wearer said the weapon looked wrong. **104.254 is
    always wrong; 88.507 is right only at 16:9.**

    The console queue holds exactly one command, so console sends are spaced.

.PARAMETER RunDir
    The launcher's run directory. Defaults to the most recent one.

.PARAMETER IpdMillimetres
    Interpupillary distance for the synthetic stereo pair.

.EXAMPLE
    ./tools/Invoke-PreyVRStartup.ps1
#>
[CmdletBinding()]
param(
    # Render height for the headset. The runtime asks for 2688x2880 per eye and
    # Prey supplies a 16:9 desktop frame, so the compositor upscales 2x
    # vertically -- 48% of the pixels it wanted. Set BEFORE xr.start, because
    # the XR swapchain is sized once from the backbuffer and a later resize is
    # refused rather than copied (it would need a rescaling blit D3D11 does not
    # do). 0 leaves the game's own resolution alone.
    [int]$RenderWidth = 0,
    [int]$RenderHeight = 0,
    # 1 = off, 2 = 2x2 SSAA, 3 = 3x3. Genuine extra detail resolved down, unlike
    # upscaling. 2 is four times the scene pixels: check the frame time.
    [int]$Supersampling = 0,
    [string]$RunDir = '',
    # Direct channel directory, for a game not started by the launcher: the DLL
    # defaults to <Documents>\PreyVR, which has no run directory or 'log' subfolder.
    [string]$LogDir = '',
    [int]$IpdMillimetres = 64,
    [int]$HalfFovDegrees = 50,
    [switch]$SkipRecenter,
    # Arms the motion-control scheme: left stick moves, right stick turns,
    # right trigger fires, and the controller drives menus. Off by default
    # because it posts synthesised input into the engine, which is a write and
    # every write in this project is opt-in.
    [switch]$Controls,
    # Arm rig from the controllers, position and rotation. On with -Controls
    # unless -NoHands is given, because a VR body whose arms do not follow the
    # hands is not a baseline anybody wants to opt into each session.
    [switch]$NoHands,
    # Compression of the player's reach into the character's, as a percentage.
    # BELOW 100 is the working range: see the note at the call site.
    [int]$IkReachPercent = 65,
    # Moves Prey's own crosshair to where the controller points.
    [switch]$Reticle,
    # How much of the headset's view the flat mirror spans, as a percentage of
    # Prey's own declared field. The MENU measures at 41% of the view at 100 and
    # 83% at 200 (R-113), so this is the knob that makes menus readable. It only
    # affects frames with no held eye pair; the stereo path uses the runtime's
    # own per-eye field and ignores it.
    [int]$MirrorFov = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $RunDir -and -not $LogDir) {
    $latest = Get-ChildItem "$env:LOCALAPPDATA\PreyVR\runs" -Directory -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { Write-Error 'no run directory found; launch the game first'; exit 1 }
    $RunDir = $latest.FullName
}
$log = if ($LogDir) { $LogDir } else { Join-Path $RunDir 'log' }
$cmd = Join-Path $log 'commands.txt'
$res = Join-Path $log 'results.txt'
if (-not (Test-Path -LiteralPath $log)) { Write-Error "no channel at $log"; exit 1 }
Write-Host "channel: $log"

function Send-Cmd($line) {
    $before = [datetime]::MinValue
    if (Test-Path -LiteralPath $res) { $before = (Get-Item -LiteralPath $res).LastWriteTime }
    Set-Content -LiteralPath $cmd -Value $line -Encoding ASCII
    $deadline = (Get-Date).AddSeconds(25)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
        if (Test-Path -LiteralPath $res) {
            $item = Get-Item -LiteralPath $res
            if ($item.LastWriteTime -gt $before) {
                $text = Get-Content -LiteralPath $res -Raw
                if ($text) { return $text.Trim() }
            }
        }
    }
    return '(no result)'
}

$failures = 0
function Step($label, $line, [switch]$AllowNonZero) {
    $reply = Send-Cmd $line
    # A status verb prints status=name(n); a result verb prints result=n. Only the
    # latter is a failure when non-zero -- reporting a status as an error is a
    # mistake this project has already made three times.
    $bad = $false
    if (-not $AllowNonZero -and $reply -match 'result=(\d+)' -and [int]$Matches[1] -ne 0) { $bad = $true }
    if ($reply -eq '(no result)') { $bad = $true }
    if ($bad) { $script:failures++ ; Write-Host ("  FAIL {0,-22} {1}" -f $label, $reply) }
    else { Write-Host ("  ok   {0,-22} {1}" -f $label, $reply) }
}

Write-Host ''
Write-Host 'before the session exists -- these cannot be changed afterwards:'
# Resolution first of all: the swapchain is built from whatever the backbuffer
# is when xr.start runs, so this has to land before it or it does nothing this
# session and is refused for the next frames.
if ($RenderWidth -gt 0 -and $RenderHeight -gt 0) {
    Step 'render width'  ("console r_Width {0}" -f $RenderWidth)
    Start-Sleep -Milliseconds 800
    Step 'render height' ("console r_Height {0}" -f $RenderHeight)
    Start-Sleep -Milliseconds 800
}
if ($Supersampling -gt 0) {
    Step 'supersampling' ("console r_Supersampling {0}" -f $Supersampling)
    Start-Sleep -Milliseconds 800
}
# Gamma. Must be before xr.start: the swapchain format is chosen at creation.
Step 'gamma (sRGB)'      'xr.srgb 1'
# Preserve PREY's projection; this does not select the runtime's eye FOVs.
# xr.stereo adds eye translation and its half-FOV argument is ignored in this mode.
# Defaults off in the DLL; FAIL-STR-048.
Step 'native projection' 'xr.native 1'
Step 'frame observer'    'observer 1' -AllowNonZero

Write-Host ''
Write-Host 'session and stereo:'
Step 'xr.start'  'xr.start'  -AllowNonZero
Step 'stereo submission' 'xr.submit 1' -AllowNonZero
Step 'stereo pair' ("xr.stereo {0} {1}" -f $IpdMillimetres, $HalfFovDegrees) -AllowNonZero
Step 'near-pass depth' 'near.enable 1'

Write-Host ''
Write-Host 'renderer settings (console queue holds one command, so these are spaced):'
Step 'motion blur off' 'console r_MotionBlur 0'
Start-Sleep -Milliseconds 800
# SMAA 1X. **Any temporal mode ghosts**, because this mod renders one eye per
# frame into a single backbuffer, so the history buffer always holds the OTHER
# eye and blends it in -- seen as a steady second weapon at the other eye's
# position (H-022b, confirmed live 2026-09-08). Read from the binary:
#   0 NO AA | 1 SMAA 1X | 2 SMAA 1TX | 3 SMAA 2TX | 4 TSAA | 5 FXAA 1X
# The T modes are temporal. Only 0, 1 and 5 are safe while eyes alternate, and
# a wearer picked 1 over 2 directly. 3 was the previous default and is the worst
# offender. Revisit this only when the eye history stops mixing.
Step 'antialiasing SMAA 1X' 'console r_AntialiasingMode 1'
Start-Sleep -Milliseconds 800
# Derived from the aspect, not hardcoded -- see the header. The horizontal field
# is held at the 120 degrees a wearer accepted; the vertical follows from that.
# A render aspect is needed, so this falls back to the value that is correct at
# 16:9 rather than inventing one when the size is unknown.
# Asked for HERE rather than reusing the report printed further down: this needs
# the size before the value is sent, and reading a variable that is assigned
# later would silently take the 16:9 fallback every run.
$nearAspect = 0.0
$backbuffer = Send-Cmd 'xr.resolution'
if ($backbuffer -match 'backbuffer=(\d+)x(\d+)' -and [int]$Matches[2] -gt 0) {
    $nearAspect = [double]$Matches[1] / [double]$Matches[2]
}
if ($nearAspect -gt 0) {
    $nearFov = [Math]::Round(
        2.0 * [Math]::Atan([Math]::Tan(60.0 * [Math]::PI / 180.0) / $nearAspect) * 180.0 / [Math]::PI, 3)
    Write-Host ("  (near FoV derived: aspect {0:N4} -> {1} deg vertical, 120 deg horizontal)" -f $nearAspect, $nearFov)
} else {
    $nearFov = 88.507
    Write-Host '  (near FoV: no backbuffer size read, using the 16:9 value 88.507)'
}
Step "weapon FoV $nearFov" "console r_DrawNearFoV $nearFov"
# **And assert it where the renderer actually reads it, every frame.**
#
# The cvar above is not what the near pass consumes: RT_BeginFrame latches it
# once per frame into CD3D9Renderer+0x95B4 (R-069), and Prey's zoom manager
# rewrites the cvar continuously -- hard-coding 55 on reset and rescaling it with
# the horizontal FOV while zooming. That is why a level load has always lost this
# setting and why it had to be reapplied by hand after every load.
#
# Both are sent: the cvar keeps the console reading the truth, and this makes the
# viewmodel actually obey it. Decidegrees, so the channel stays integral.
Step "weapon FoV asserted" ("near.fov " + [int][Math]::Round($nearFov * 10))
Start-Sleep -Milliseconds 800

Write-Host ''
Write-Host 'head tracking (recenter first, or apply returns 2):'
Step 'view observe' 'view.observe 1'
if (-not $SkipRecenter) {
    Write-Host '  -- face forward now --'
    Step 'recenter' 'view.recenter'
}
Step 'head rotation' 'view.apply 1'
Step 'head 6DoF'     'view.position 1'

if ($MirrorFov -gt 0) {
    Write-Host ''
    Write-Host 'flat mirror field:'
    Step 'mirror fov' "xr.mirrorfov $MirrorFov"
}

if ($Controls -or $Reticle) {
    Write-Host ''
    $Hands = $Controls -and -not $NoHands
    Write-Host 'motion controls and reticle:'
    if ($Controls) {
        # Menu navigation first: it is the one that works at the main menu,
        # where the others have no player to drive.
        Step 'menu navigation' 'menu.nav 1'
        Step 'move/turn/fire'  'move.all 1'
    }
    if ($Reticle) {
        Step 'aim takeover'  'aim.enable 1'
        Step 'reticle follow' 'aim.reticle 1'
        # On by default in the DLL; set explicitly so the transcript records
        # which half was armed. The write alone moves nothing -- the engine's
        # own reset writes the field AND dispatches, and so must this (R-109).
        Step 'reticle dispatch' 'aim.reticledispatch 1'
    }

    # --- arm rig ------------------------------------------------------------
    #
    # **Part of the baseline now, not a separate ritual.** Position and rotation
    # both come from the controllers, so the arms belong with stereo rather than
    # behind four commands a wearer has to remember.
    #
    # Order matters. `ik.drive` must be on before `ik.calibrate`, which refuses
    # outright without it, and the calibration captures on the next SOLVED frame
    # with a tracked controller -- so the wearer has to be holding still when it
    # lands, which is why it is prompted rather than fired silently.
    if ($Hands) {
        Step 'arm IK apply'    'ik.mode 2'
        Step 'arm IK drive'    'ik.drive 1'
        Step 'both arms'       'ik.hands 3'
        # **Below 100 on purpose.** ScaleReach compresses the player's reach into
        # the character's BEFORE the clamp, so 100 means no compression and a
        # normal arm extension runs straight into the clamp. Measured live at
        # 2026-09-09: reach 100 clamped roughly nine frames in ten, 80 clamped
        # four in ten, and 65 clamped under one in twenty. Raising this number
        # does not lengthen the arm -- it removes the compression that keeps the
        # motion continuous, which is the opposite of what it sounds like.
        Step 'reach compression' "ik.reach $IkReachPercent"
        Write-Host '  -- hold both controllers still --'
        Start-Sleep -Seconds 3
        Step 'arm calibration' 'ik.calibrate'
        # Reported rather than assumed: `ik.calibrate` returning 0 means the
        # request was accepted, never that it was captured. Only ikCalR/ikCalL
        # say it landed, and a weapon change silently invalidates both.
        Start-Sleep -Milliseconds 800
        $ik = Send-Cmd 'report'
        $flags = ($ik -split ' ' | Where-Object { $_ -match '^ik(CalR|CalL)=' }) -join ' '
        if ($flags -match 'ikCalR=1' -and $flags -match 'ikCalL=1') {
            Write-Host "  ok   calibration captured  $flags"
        } else {
            Write-Host "  WARN calibration NOT captured  $flags"
            Write-Host '       hold the controllers still and re-run: ik.calibrate'
        }
    }
}

Write-Host ''
Start-Sleep -Seconds 3
$resolution = Send-Cmd 'xr.resolution'
Write-Host 'resolution chain:'
Write-Host ("  " + $resolution)
Write-Host ''
$report = Send-Cmd 'report'
$want = 'xrSession|xrFrames|viewApplied|posApplied|nearApplied|cameraEdit|lastEye'
Write-Host 'state:'
($report -split ' ' | Where-Object { $_ -match $want }) | ForEach-Object { Write-Host "  $_" }

Write-Host ''
if ($failures -gt 0) {
    Write-Host ("startup completed with {0} failed step(s) -- read the FAIL lines above" -f $failures)
    exit 1
}
Write-Host 'startup complete.'
Write-Host ''
Write-Host 'Next: load a save and EQUIP A WEAPON, then run Invoke-PreyVRIkTest.ps1.'
Write-Host '  - -Controls arms the motion scheme (left stick move, right stick turn,'
Write-Host '    right trigger fire, controller menus). -Reticle moves the crosshair.'
Write-Host '  - Judge the reticle by reticleDispatched climbing with'
Write-Host '    reticleDispatchFailed at zero, THEN by eye. A dispatch returning 0'
Write-Host '    proves the ABI, not that the movie has that function (F-011).'
Write-Host '  - -RenderHeight 2880 -RenderWidth 2688 matches the runtime request;'
Write-Host '    it must be set before xr.start, which is why it is a parameter here.'
Write-Host '  - hand.calibrate belongs to the OLD hand lane. For the IK lane use'
Write-Host '    ik.calibrate, and leave hand.mode at 0 -- the two lanes refuse each'
Write-Host '    other because running both applies the controller twice.'
Write-Host '  - Re-equip the weapon AFTER ik.mode 1 arms: ownership is captured on'
Write-Host '    the attach call, so the first observe run reports ikOwner=0x0.'
Write-Host '  - The IK script drives the right hand by default; pass -Hands 3 for'
Write-Host '    both, then calibrate once with both controllers tracked.'
