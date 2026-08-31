<#
.SYNOPSIS
    Commands a known controller aim in xr-sim and checks the engine-space ray
    our code produces against it.

.DESCRIPTION
    The session probe's own invariants prove the basis change is *self-consistent*
    -- rigid, unit length, forward agreeing with the pose. They cannot prove it
    agrees with reality, because the probe has no ground truth: it converts
    whatever the runtime hands it.

    This supplies the ground truth. xr-sim can be commanded to point a controller
    at an exact yaw and pitch, so the expected engine-space direction follows from
    geometry alone and the comparison is a real test rather than a restatement.

    **Magnitudes are asserted; signs are reported.** The component magnitudes are
    pure trigonometry and must match. The signs depend on which way xr-sim counts
    a positive yaw, which is a convention we have not measured -- so this prints
    it and records it rather than assuming a value and calling a mismatch a bug.

    **All cases run inside one session, correlated by frame number.** The first
    attempt at this ran a process per case and lost every one of them: xr-sim
    ignores a command.txt written before its run starts, and the liveness check
    was satisfied by a stale state.json from the previous case, so the commands
    were acknowledged by nothing. Every case then returned the identical default
    pose and looked like four failures rather than one broken harness. Hence:
    state.json is deleted before launch so its reappearance proves a new run, and
    every reading is stamped with the frame it came from so a reading taken before
    a command cannot be read as the answer to it.

.EXAMPLE
    ./tools/Invoke-PreyVRAimCheck.ps1
#>
[CmdletBinding()]
param(
    [string]$XrSimRoot = 'D:\Dev Debug\xr-sim',
    [string]$Probe = 'build/configure/Release/preyvr_xr_session_probe.exe',
    [string]$StateName = 'preyvr',
    [int]$Frames = 3000,
    [int]$InputEvery = 30,
    [double]$Tolerance = 0.02
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$stateDir = Join-Path $env:LOCALAPPDATA "xr-sim\$StateName"
$statePath = Join-Path $stateDir 'state.json'
$cmdScript = Join-Path $XrSimRoot 'tools\xrsim-cmd.ps1'
foreach ($required in @($cmdScript, $Probe)) {
    if (-not (Test-Path -LiteralPath $required)) {
        Write-Error "not found: $required"
        exit 1
    }
}

function Get-SimFrame {
    for ($k = 0; $k -lt 5; $k++) {
        try { return [int](Get-Content $statePath -Raw -ErrorAction Stop | ConvertFrom-Json).frame }
        catch { Start-Sleep -Milliseconds 100 }
    }
    return -1
}

# Each case commands one rotation away from straight ahead, so the expected
# direction is a single sine and cosine rather than a composed rotation whose
# order would itself be an assumption.
$cases = @(
    [pscustomobject]@{ Name = 'forward'; Yaw = 0;  Pitch = 0 }
    [pscustomobject]@{ Name = 'yaw45';   Yaw = 45; Pitch = 0 }
    [pscustomobject]@{ Name = 'yaw90';   Yaw = 90; Pitch = 0 }
    [pscustomobject]@{ Name = 'pitch30'; Yaw = 0;  Pitch = 30 }
)

# Staleness is the failure mode this whole script had to be rewritten for, so the
# old state file is removed rather than trusted to be overwritten in time.
if (Test-Path -LiteralPath $statePath) { Remove-Item -LiteralPath $statePath -Force }

$job = Start-Job -ScriptBlock {
    param($root, $probe, $state, $frames, $every, $cwd)
    Set-Location $cwd
    & (Join-Path $cwd 'tools\Invoke-PreyVRUnderXrSim.ps1') `
        -XrSimRoot $root -Executable $probe -StateName $state `
        -Arguments @("$frames", '--input-every', "$every") 2>&1
} -ArgumentList $XrSimRoot, $Probe, $StateName, $Frames, $InputEvery, (Get-Location).Path

try {
    $waited = 0.0
    while ($waited -lt 60 -and (Get-SimFrame) -lt 5) {
        Start-Sleep -Milliseconds 200
        $waited += 0.2
    }
    $startFrame = Get-SimFrame
    if ($startFrame -lt 5) {
        Write-Error 'the sim never started advancing frames'
        Stop-Job $job -ErrorAction SilentlyContinue | Out-Null
        Remove-Job $job -Force -ErrorAction SilentlyContinue | Out-Null
        exit 1
    }
    Write-Host "sim live at frame $startFrame"

    foreach ($case in $cases) {
        & $cmdScript -Dir $stateDir -Quiet `
            'hands reset' `
            'hand r follow off' `
            ("hand r point {0} {1}" -f $case.Yaw, $case.Pitch) | Out-Null

        $applied = Get-SimFrame
        Add-Member -InputObject $case -NotePropertyName 'CommandFrame' -NotePropertyValue $applied
        Write-Host ("commanded $($case.Name) at frame $applied")

        # Wait past the command frame by more than one reporting interval, so a
        # reading attributed to this case cannot have been taken before it.
        $target = $applied + (2 * $InputEvery)
        $spun = 0.0
        while ($spun -lt 20 -and (Get-SimFrame) -lt $target) {
            Start-Sleep -Milliseconds 100
            $spun += 0.1
        }
    }
} finally {
    $output = Receive-Job $job -Wait -AutoRemoveJob
}

$rays = @()
foreach ($line in $output) {
    if ("$line" -match 'aim_ray frame=(\d+) hand=right .*direction=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+)') {
        $rays += [pscustomobject]@{
            Frame = [int]$Matches[1]
            X = [double]$Matches[2]; Y = [double]$Matches[3]; Z = [double]$Matches[4]
        }
    }
}
if ($rays.Count -eq 0) {
    Write-Error 'no right-hand aim rays in the probe output'
    exit 1
}

$results = @()
$failures = 0
$rad = [Math]::PI / 180.0

foreach ($case in $cases) {
    # The first reading strictly after the command, and before the next command.
    $next = @($cases | Where-Object { $_.CommandFrame -gt $case.CommandFrame } |
        Sort-Object CommandFrame | Select-Object -First 1)
    $upper = if ($next.Count -eq 1) { $next[0].CommandFrame } else { [int]::MaxValue }
    $match = @($rays | Where-Object { $_.Frame -gt $case.CommandFrame -and $_.Frame -lt $upper } |
        Sort-Object Frame | Select-Object -First 1)

    if ($match.Count -ne 1) {
        Write-Warning "case $($case.Name): no reading landed between frames $($case.CommandFrame) and $upper"
        $failures++
        continue
    }
    $r = $match[0]

    # Engine space is Z-up with +Y forward. A yaw-only aim must stay horizontal
    # (no Z), a pitch-only aim must stay in the vertical plane (no X), and the
    # remaining components are cos and sin of the commanded angle.
    if ($case.Pitch -eq 0) {
        $expectedForward = [Math]::Cos($case.Yaw * $rad)
        $expectedSide    = [Math]::Sin($case.Yaw * $rad)
        $expectedVert    = 0.0
    } else {
        $expectedForward = [Math]::Cos($case.Pitch * $rad)
        $expectedSide    = 0.0
        $expectedVert    = [Math]::Sin($case.Pitch * $rad)
    }

    $ok = ([Math]::Abs([Math]::Abs($r.Y) - $expectedForward) -lt $Tolerance) -and
          ([Math]::Abs([Math]::Abs($r.X) - $expectedSide) -lt $Tolerance) -and
          ([Math]::Abs([Math]::Abs($r.Z) - $expectedVert) -lt $Tolerance)
    if (-not $ok) { $failures++ }

    $results += [pscustomobject]@{
        Case = $case.Name; Yaw = $case.Yaw; Pitch = $case.Pitch
        Frame = $r.Frame
        X = [Math]::Round($r.X, 4); Y = [Math]::Round($r.Y, 4); Z = [Math]::Round($r.Z, 4)
        WantAbsX = [Math]::Round($expectedSide, 4)
        WantAbsY = [Math]::Round($expectedForward, 4)
        WantAbsZ = [Math]::Round($expectedVert, 4)
        Pass = $ok
    }
}

$results | Format-Table -AutoSize | Out-String | Write-Host

# A repeated direction across every case is the signature of commands that never
# landed, which is exactly how this harness failed the first time. Named here so
# the next person does not have to rediscover it from four identical rows.
$distinct = @($results | ForEach-Object { "{0:F3},{1:F3},{2:F3}" -f $_.X, $_.Y, $_.Z } |
    Sort-Object -Unique)
if ($results.Count -gt 1 -and $distinct.Count -eq 1) {
    Write-Host 'WARNING: every case returned the same direction - the commands did not reach the sim,'
    Write-Host '         so these rows measure nothing regardless of whether they pass.'
}

# The conventions this run measured, stated so they can be recorded rather than
# rediscovered. A sign is not a pass/fail -- it is a fact about xr-sim.
foreach ($probeCase in @(
        @{ Filter = { $_.Yaw -eq 90 }; Axis = 'X'; Pos = 'engine +X (right)'; Neg = 'engine -X (left)'; What = 'yaw' },
        @{ Filter = { $_.Pitch -eq 30 }; Axis = 'Z'; Pos = 'engine +Z (up)'; Neg = 'engine -Z (down)'; What = 'pitch' })) {
    $row = @($results | Where-Object $probeCase.Filter)
    if ($row.Count -eq 1) {
        $value = $row[0].($probeCase.Axis)
        $sense = if ($value -gt 0) { $probeCase.Pos } else { $probeCase.Neg }
        Write-Host ("convention: a positive xr-sim {0} sends the ray toward {1}" -f $probeCase.What, $sense)
    }
}

Write-Host ''
Write-Host ("aim check: {0} case(s), {1} failure(s)" -f $results.Count, $failures)
if ($failures -gt 0) { exit 1 }
