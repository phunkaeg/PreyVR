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

    **`r_DrawNearFoV` is 88.507, not 104.254.** The FOV buffer reads
    `[0, tanLeft, tanRight, tanUp]`; taking indices 2 and 3 as the vertical pair
    mixes a horizontal tangent with a vertical one and yields 104.254, which was
    set live twice before a wearer said the weapon looked wrong.

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
    [string]$RunDir = '',
    [int]$IpdMillimetres = 64,
    [int]$HalfFovDegrees = 50,
    [switch]$SkipRecenter
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $RunDir) {
    $latest = Get-ChildItem "$env:LOCALAPPDATA\PreyVR\runs" -Directory -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { Write-Error 'no run directory found; launch the game first'; exit 1 }
    $RunDir = $latest.FullName
}
$log = Join-Path $RunDir 'log'
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
# Gamma. Must be before xr.start: the swapchain format is chosen at creation.
Step 'gamma (sRGB)'      'xr.srgb 1'
# Native per-eye projection. Defaults off; FAIL-STR-048.
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
# 88.507 -- see the header. Not 104.254.
Step 'weapon FoV 88.507' 'console r_DrawNearFoV 88.507'
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

Write-Host ''
Start-Sleep -Seconds 3
$report = Send-Cmd 'report'
$want = 'xrSession|xrFrames|viewApplied|posApplied|nearApplied|cameraEdit|lastEye'
Write-Host 'state:'
($report -split ' ' | Where-Object { $_ -match $want }) | ForEach-Object { Write-Host "  $_" }

Write-Host ''
if ($failures -gt 0) {
    Write-Host ("startup completed with {0} failed step(s) -- read the FAIL lines above" -f $failures)
    exit 1
}
Write-Host 'startup complete. Motion controllers: hold both where the headset can see'
Write-Host 'them, then run hand.calibrate -- it refuses unless BOTH are tracked, on'
Write-Host 'purpose, so one sleeping controller blocks the whole hand lane.'
