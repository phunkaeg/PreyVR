<#
.SYNOPSIS
    Sweeps a virtual motion controller through a rotation and captures what the
    headset compositor would show at each step.

.DESCRIPTION
    This is the observation loop the project was missing. Every live finding so
    far has come back as a sentence from whoever was wearing the headset --
    "weapon depth looks correct", "no change, looks identical" -- because nothing
    else could see the screen. Counters said the takeover applied; only a person
    could say whether it looked right, and four times running the instruments
    were green while the screen was wrong.

    xr-sim closes that. It is a real OpenXR runtime with scriptable poses, and on
    D3D11 it composites and captures **what it was handed, per eye**. So a
    controller can be commanded to a known rotation and the resulting frame read
    back as an image, with a JSON sidecar of the numbers behind it.

    **This attaches; it does not launch.** Prey must already be running under
    xr-sim with the mod loaded and a level in view. Starting the game is the one
    step that still needs a person: the main menu answers to a keyboard, and
    nothing here can press one.

.PARAMETER Steps
    How many samples across the range. The default walks a half circle in 15
    degree steps, which is coarse enough to stay quick and fine enough that a
    mount composed in the wrong order shows up as motion in the wrong direction
    rather than as a single odd frame.

.PARAMETER Axis
    'yaw' or 'pitch'. Sweeping one at a time keeps the expected result a single
    trigonometric term, which is the same reason the aim check commands one
    rotation per case.

.EXAMPLE
    ./tools/Invoke-PreyVRControllerSweep.ps1 -Out "$env:TEMP\sweep"
#>
[CmdletBinding()]
param(
    [string]$XrSimRoot = 'D:\Dev Debug\xr-sim',
    [string]$StateName = 'preyvr',
    [string]$Out = "$env:TEMP\preyvr-sweep",
    [ValidateSet('yaw', 'pitch')]
    [string]$Axis = 'yaw',
    [double]$From = -90.0,
    [double]$To = 90.0,
    [int]$Steps = 13,
    [ValidateSet('l', 'r')]
    [string]$Hand = 'r',
    [int]$SettleMs = 350
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$stateDir   = Join-Path $env:LOCALAPPDATA "xr-sim\$StateName"
$statePath  = Join-Path $stateDir 'state.json'
$cmdScript  = Join-Path $XrSimRoot 'tools\xrsim-cmd.ps1'
$shotScript = Join-Path $XrSimRoot 'tools\xrsim-shot.ps1'

foreach ($required in @($cmdScript, $shotScript, $statePath)) {
    if (-not (Test-Path -LiteralPath $required)) {
        Write-Error "not found: $required"
        exit 1
    }
}

# Preflight, each check named for the failure it prevents. A sweep that runs
# against a stopped session produces a folder of identical black frames and
# looks like a mod that does nothing.
$state = Get-Content $statePath -Raw | ConvertFrom-Json
if ($state.graphics -ne 'D3D11') {
    Write-Error "image capture needs the D3D11 backend; this session is $($state.graphics)"
    exit 1
}
if (-not $state.sessionRunning) {
    Write-Error 'no running xr-sim session - start the game under xr-sim first'
    exit 1
}
if ($state.sessionState -ne 'FOCUSED' -and $state.sessionState -ne 'VISIBLE') {
    Write-Error "session is $($state.sessionState); a capture now would be black by construction"
    exit 1
}
Write-Host ("session ok: state={0} frame={1} graphics={2}" -f `
    $state.sessionState, $state.frame, $state.graphics)

New-Item -ItemType Directory -Path $Out -Force | Out-Null

if ($Steps -lt 2) { Write-Error 'a sweep needs at least two steps'; exit 1 }
$stride = ($To - $From) / ($Steps - 1)

# The controller must stop following the head, or the sweep is fighting a
# follow rule and the angle that arrives is not the angle commanded.
& $cmdScript -Dir $stateDir -Quiet 'hands reset' "hand $Hand follow off" | Out-Null

$rows = @()
for ($i = 0; $i -lt $Steps; $i++) {
    $angle = $From + ($stride * $i)
    if ($Axis -eq 'yaw') { $yaw = $angle; $pitch = 0.0 } else { $yaw = 0.0; $pitch = $angle }

    # Invariant formatting: `-f` renders 0.5 as "0,5" under a comma-decimal
    # locale and the simulator would reject or misread it.
    $command = [string]::Format([cultureinfo]::InvariantCulture,
                                'hand {0} point {1} {2}', $Hand, $yaw, $pitch)
    & $cmdScript -Dir $stateDir -Quiet $command | Out-Null
    Start-Sleep -Milliseconds $SettleMs

    $name = 'step-{0:d2}' -f $i
    $target = Join-Path $Out $name
    $captured = $false
    try {
        & $shotScript -Dir $stateDir -Out $target -Quiet | Out-Null
        $captured = $true
    } catch {
        # The shot tool can time out waiting for its own sequence to advance
        # while still having written the frame. Judge by the file, not the
        # exception -- a discarded good capture is worse than a noisy one.
        Write-Warning "$name : shot reported '$($_.Exception.Message)'"
    }

    $sidecar = Join-Path $stateDir "capture\$name.json"
    if (-not (Test-Path -LiteralPath $sidecar)) {
        Write-Warning "$name : no capture landed"
        continue
    }
    $meta = Get-Content $sidecar -Raw | ConvertFrom-Json
    $rows += [pscustomobject]@{
        Step      = $i
        Angle     = [Math]::Round($angle, 2)
        Frame     = $meta.frameIndex
        Layers    = $meta.layerCount
        MeanLumaL = [Math]::Round($meta.stats.meanLumaL, 2)
        MeanLumaR = [Math]::Round($meta.stats.meanLumaR, 2)
        NonBlackL = [Math]::Round($meta.stats.nonBlackPctL, 2)
        AimYprR   = ($meta.handR.aimYpr -join ',')
        Reported  = $captured
        Sbs       = Join-Path $stateDir "capture\${name}_sbs.png"
    }
    Write-Host ("{0} angle={1,7:n2} frame={2} luma={3}/{4} layers={5}" -f `
        $name, $angle, $meta.frameIndex, `
        [Math]::Round($meta.stats.meanLumaL, 1), `
        [Math]::Round($meta.stats.meanLumaR, 1), $meta.layerCount)
}

& $cmdScript -Dir $stateDir -Quiet 'hands reset' | Out-Null

if ($rows.Count -eq 0) {
    Write-Error 'the sweep captured nothing'
    exit 1
}

$rows | Format-Table -AutoSize | Out-String | Write-Host

# **An unchanging sweep is the failure this is most likely to have.** If every
# frame is identical the commands did not reach the game, or the thing being
# swept is not connected to anything -- and a folder of identical images looks
# exactly like a successful run until someone opens two of them.
$distinct = @($rows | ForEach-Object { '{0}/{1}' -f $_.MeanLumaL, $_.MeanLumaR } |
    Sort-Object -Unique)
if ($rows.Count -gt 1 -and $distinct.Count -eq 1) {
    Write-Host ''
    Write-Host 'WARNING: every frame has identical luminance. Either the commands did not reach'
    Write-Host '         the sim, or nothing in view responds to the controller. Compare two'
    Write-Host '         captures before reading anything into this sweep.'
}

$index = Join-Path $Out 'sweep.json'
$rows | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $index -Encoding UTF8
Write-Host ''
Write-Host ("captured {0} step(s); index: {1}" -f $rows.Count, $index)
Write-Host ("frames:  {0}" -f (Join-Path $stateDir 'capture'))
