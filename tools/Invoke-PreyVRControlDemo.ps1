<#
.SYNOPSIS
    Drives the motion control scheme from commanded controller values and reports
    what each lane did, without anyone wearing a headset.

.DESCRIPTION
    The control scheme was built and never demonstrated. Counters said the hooks
    were installed; nothing showed a stick deflection travelling all the way from
    an OpenXR action to a posted engine input event. This closes that, because
    xr-sim is a real OpenXR runtime with scriptable controls: `stick r 0 1` puts
    a genuine Vector2f through the same action the headset would.

    **It cannot tell the hands apart, and neither can this script.** xr-sim keeps
    one value per action, so an action bound to both hands collapses to whichever
    binding resolved last. Both subaction queries then return that one stick. The
    mod's own setup is correct per the spec; left-from-right separation is simply
    outside what this harness can observe, and needs a headset.

    **What this proves and what it does not.** It proves the chain from commanded
    controller to posted engine event: XR action read, deadzone shaping, key id,
    and the post itself. It does NOT prove the player moves -- the analog handlers
    it feeds belong to a live player, so at the main menu there is nothing to
    drive. Read `movePosted` climbing as "the event was posted", and only a loaded
    level plus `moveOursX/Y` against `moveNative` as "the player moved".

    Each case commands ONE axis. A stick pushed diagonally would make a failure
    ambiguous between the two axes and the deadzone rescale at once.

.PARAMETER SimDir
    The run's own xr-sim state directory. Defaults to the newest run, because a
    shared directory is how a previous session's state was once read as a live
    one.

.EXAMPLE
    ./tools/Invoke-PreyVRControlDemo.ps1
#>
[CmdletBinding()]
param(
    [string]$SimDir = '',
    [string]$LogDir = '',
    [string]$XrSimRoot = 'D:\Dev Debug\xr-sim',
    [int]$SettleMs = 900
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$latest = $null
if (-not $SimDir -or -not $LogDir) {
    $latest = Get-ChildItem "$env:LOCALAPPDATA\PreyVR\runs" -Directory -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { Write-Error 'no run directory found; launch the game first'; exit 1 }
}
if (-not $SimDir) { $SimDir = Join-Path $latest.FullName 'xrsim' }
if (-not $LogDir) { $LogDir = Join-Path $latest.FullName 'log' }

$cmdScript = Join-Path $XrSimRoot 'tools\xrsim-cmd.ps1'
$statePath = Join-Path $SimDir 'state.json'
foreach ($required in @($cmdScript, $statePath)) {
    if (-not (Test-Path -LiteralPath $required)) { Write-Error "not found: $required"; exit 1 }
}

# A live session, checked for real. state.json outlives the process that wrote
# it, so sessionRunning and FOCUSED persist on disk long after a run is gone.
$state = Get-Content $statePath -Raw | ConvertFrom-Json
$owner = Get-Process -Id $state.pid -ErrorAction SilentlyContinue
if (-not $owner -or $owner.ProcessName -ne 'Prey') {
    Write-Error "state.json claims PID $($state.pid), which is not a live Prey process"
    exit 1
}
Write-Host "sim:   $SimDir  (PID $($state.pid), frame $($state.frame))"
Write-Host "chan:  $LogDir"

$cmdFile = Join-Path $LogDir 'commands.txt'
$resFile = Join-Path $LogDir 'results.txt'

function Send-Cmd([string]$line) {
    $before = [datetime]::MinValue
    if (Test-Path -LiteralPath $resFile) { $before = (Get-Item -LiteralPath $resFile).LastWriteTime }
    Set-Content -LiteralPath $cmdFile -Value $line -Encoding ASCII
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 200
        if (Test-Path -LiteralPath $resFile) {
            $item = Get-Item -LiteralPath $resFile
            if ($item.LastWriteTime -gt $before) {
                $text = Get-Content -LiteralPath $resFile -Raw
                if ($text) { return $text.Trim() }
            }
        }
    }
    return '(no result)'
}

function Get-Counters {
    $report = Send-Cmd 'report'
    $out = @{}
    foreach ($field in @('movePosted', 'moveDropped', 'moveOursX', 'moveOursY', 'moveNative',
                         'moveAxisMilli', 'turnPosted', 'turnRefused', 'firePressed',
                         'fireReleased', 'fireRefused', 'inputPosted', 'inputDropped',
                         'moveHooked', 'moveMode', 'turnEnabled', 'fireEnabled',
                         'stickL', 'stickR', 'trigL', 'trigR', 'xrSyncs', 'xrNotFocused')) {
        if ($report -match "\b$field=([^\s]+)") { $out[$field] = $Matches[1] } else { $out[$field] = '?' }
    }
    return $out
}

function Send-Sim([string[]]$lines) {
    & $cmdScript -Dir $SimDir -Quiet @lines | Out-Null
}

Write-Host ''
Write-Host 'arming the lanes:'
foreach ($step in @('move.all 1')) {
    Write-Host ("  {0,-16} {1}" -f $step, (Send-Cmd $step))
}

# Baseline with everything centred, so each case is read as a DELTA. Absolute
# counters carry whatever the session did before this script ran.
Send-Sim @('input clear')
Start-Sleep -Milliseconds $SettleMs
$base = Get-Counters
Write-Host ''
Write-Host ("baseline: movePosted={0} turnPosted={1} firePressed={2} moveHooked={3}" -f `
    $base.movePosted, $base.turnPosted, $base.firePressed, $base.moveHooked)

# **Every stick case commands the RIGHT stick, and that is a limitation of the
# harness rather than a choice.** xr-sim stores one control per action, so an
# action bound to both hands collapses to whichever binding resolved last -- the
# right one here. Both of the mod's subaction queries then return the right
# stick's value, and `stick l` reaches nothing.
#
# The mod's setup is correct per the OpenXR spec: one action, two subaction
# paths, two bindings, queried per subaction path; a conformant runtime returns
# per-hand values. So this exercises the shaping, the key ids and the posting on
# the real code path, and does NOT test left-from-right separation. That needs a
# headset, and it is the one thing this script cannot say anything about.
$cases = @(
    [pscustomobject]@{ Name = 'stick forward';    Sim = @('stick r 0 1');    Expect = 'movePosted' }
    [pscustomobject]@{ Name = 'stick back';       Sim = @('stick r 0 -1');   Expect = 'movePosted' }
    [pscustomobject]@{ Name = 'stick right';      Sim = @('stick r 1 0');    Expect = 'movePosted' }
    [pscustomobject]@{ Name = 'stick left';       Sim = @('stick r -1 0');   Expect = 'movePosted' }
    [pscustomobject]@{ Name = 'turn (stick x)';   Sim = @('stick r 1 0');    Expect = 'turnPosted' }
    [pscustomobject]@{ Name = 'trigger half';     Sim = @('trigger r 0.5');  Expect = 'inputPosted' }
    [pscustomobject]@{ Name = 'trigger full';     Sim = @('trigger r 1.0');  Expect = 'firePressed' }
)

$rows = @()
$failures = 0
foreach ($case in $cases) {
    $before = Get-Counters
    Send-Sim $case.Sim
    Start-Sleep -Milliseconds $SettleMs
    $after = Get-Counters
    Send-Sim @('input clear')
    Start-Sleep -Milliseconds 300

    $moved = 0
    if ($before[$case.Expect] -match '^\d+$' -and $after[$case.Expect] -match '^\d+$') {
        $moved = [int64]$after[$case.Expect] - [int64]$before[$case.Expect]
    }
    $ok = $moved -gt 0
    if (-not $ok) { $script:failures++ }
    $rows += [pscustomobject]@{
        Case      = $case.Name
        Counter   = $case.Expect
        Delta     = $moved
        Axis      = $after.moveAxisMilli
        Sticks    = "$($after.stickR)"
        Trig      = $after.trigR
        Dropped   = $after.moveDropped
        Refused   = "$($after.turnRefused)/$($after.fireRefused)"
        Result    = if ($ok) { 'posted' } else { 'NO CHANGE' }
    }
}

Send-Sim @('input clear')

Write-Host ''
$rows | Format-Table -AutoSize
Write-Host ''
if ($failures -eq 0) {
    Write-Host "all $($cases.Count) control cases posted an event."
} else {
    Write-Host "$failures of $($cases.Count) cases posted NOTHING."
}
Write-Host ''
Write-Host 'Reading this:'
Write-Host '  - Delta > 0 means the commanded control reached the engine as an input'
Write-Host '    event. It does NOT mean the player moved; the analog handlers belong'
Write-Host '    to a live player, so judge movement only with a level loaded.'
Write-Host '  - Dropped climbing means the input queue overflowed. Axis events post'
Write-Host '    directly for exactly that reason, so a non-zero here is a real defect.'
Write-Host '  - Refused climbing on turn or fire means the key id was rejected, which'
Write-Host '    is the one value in the scheme inferred rather than read from a call site.'
Write-Host '  - Sticks/Trig are what the MOD read back, so a case that posts nothing'
Write-Host '    with a zero here failed before the lane rather than inside it.'
Write-Host '  - Left-from-right separation is NOT tested: xr-sim keeps one value per'
Write-Host '    action, so both hands read the same stick. Only a headset settles that.'
exit ($failures -gt 0 ? 1 : 0)
