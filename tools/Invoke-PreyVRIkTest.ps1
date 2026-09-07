<#
.SYNOPSIS
    The H-021 animation-driven-IK protocol, in order, with the pass/fail read
    for each step printed so nobody has to remember what a number means.

.DESCRIPTION
    Run AFTER Invoke-PreyVRStartup.ps1, in game, with a weapon equipped.

    Phase observe (default): captures the selected weapon owner (re-equip after
    first arming observation), then identifies its hand rig by signature and reads its
    ADIK table, limbs, the +0x610 gate and ca_useADIKTargets. This is the five
    live reads the static report asked for, and it writes nothing.

    Phase test (-TestMillimetres N): raises the animated wrist goal by N mm in
    model Z with no controller in the loop. A wearer must see the HAND AND THE
    WEAPON rise together. That single observation is items 1, 2 and 3 of H-021.

    Phase drive (-Drive): controller ownership of the wrist through the engine's
    own arm IK, then calibration. Requires head and selected controller tracked.

    hand.mode must be 0 for phases test and drive: the two lanes are mutually
    exclusive, and the script refuses rather than apply the controller twice.
#>
[CmdletBinding()]
param(
    [string]$RunDir = '',
    [int]$TestMillimetres = 0,
    [switch]$Drive,
    [int]$Joints = 101
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $RunDir) {
    $latest = Get-ChildItem "$env:LOCALAPPDATA\PreyVR\runs" -Directory -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { Write-Error 'no run directory; launch first'; exit 1 }
    $RunDir = $latest.FullName
}
$log = Join-Path $RunDir 'log'
$cmd = Join-Path $log 'commands.txt'
$res = Join-Path $log 'results.txt'

function Send($line) {
    $before = [datetime]::MinValue
    if (Test-Path -LiteralPath $res) { $before = (Get-Item -LiteralPath $res).LastWriteTime }
    Set-Content -LiteralPath $cmd -Value $line -Encoding ASCII
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
        if ((Test-Path -LiteralPath $res) -and (Get-Item -LiteralPath $res).LastWriteTime -gt $before) {
            return (Get-Content -LiteralPath $res -Raw).Trim()
        }
    }
    return '(no result)'
}
function Field($report, $name) {
    if ($report -match "(?:^| )$name=([^ ]+)") { return $Matches[1] }
    return '?'
}

Write-Host "channel: $log"
Write-Host ''
Write-Host 'phase observe -- identify the rig and read what the static report inferred:'
Write-Host ('  ' + (Send "ik.joints $Joints"))
Write-Host ('  ' + (Send 'ik.mode 1'))
Start-Sleep -Seconds 3
$r = Send 'report'
$calls = Field $r 'ikCalls'
$matched = Field $r 'ikMatched'
Write-Host "  ikCalls=$calls        (0 => the pass never ran: no animation commands (+0x610 = 0), cvar off, physics state, or hook problem)"
Write-Host "  ikMatched=$matched    (0 => first verify re-equip capture, selected item, owning character, then the $Joints-joint signature)"
Write-Host ('  ikOwner=' + (Field $r 'ikOwner') + ' ikEquipGen=' + (Field $r 'ikEquipGen') + ' ikNoOwner=' + (Field $r 'ikNoOwner') + ' ikPoseSeq=' + (Field $r 'ikPoseSeq'))
Write-Host ('  ikRig=' + (Field $r 'ikRig') + ' ikJoints=' + (Field $r 'ikJoints') + ' ikGate=' + (Field $r 'ikGate') + ' ikCvar=' + (Field $r 'ikCvar') + '   (gate = m_IsAnimPlaying, not IK presence; the table is the target/weight lines below)')
Write-Host ('  right: target=' + (Field $r 'ikTargetR') + ' weight=' + (Field $r 'ikWeightR') + ' limbEnd=' + (Field $r 'ikLimbEndR') + ' tag=' + (Field $r 'ikLimbTagR') + '   (expect 38 / 4 / 45 / 0x4b494232 = 2BIK)')
Write-Host ('  left:  target=' + (Field $r 'ikTargetL') + ' weight=' + (Field $r 'ikWeightL') + ' limbEnd=' + (Field $r 'ikLimbEndL') + '   (expect 39 / 5 / 72)')
Write-Host ('  ikLocMm=' + (Field $r 'ikLocMm') + ' ikLocYawMdeg=' + (Field $r 'ikLocYawMdeg') + '   (the rig model->world; compare with the player position and facing)')
Write-Host ('  weaponBone=' + (Field $r 'weaponBone') + ' weaponSim=' + (Field $r 'weaponSim') + '   (bone the weapon hangs from, expect 45 or 47; sim bits: low byte spring on, high byte redirect)')
Write-Host ('  ' + (Send 'ik.dump') + '  -- full ADIK table and limbs are in PreyVR.log')
if ($matched -eq '0' -or $calls -eq '0' -or (Field $r 'ikOwner') -eq '0x0') {
    Write-Host ''
    Write-Host 'stop here: re-equip after ik.mode 1, then rerun; verify ownership and native gates before changing the signature.'
    exit 1
}

if (($TestMillimetres -ne 0 -or $Drive) -and (Field $r 'handMode') -eq '2') {
    Write-Host ''
    Write-Host 'refusing: hand.mode is 2. Send "hand.mode 0" first -- the lanes are mutually exclusive.'
    exit 1
}

if ($TestMillimetres -ne 0) {
    Write-Host ''
    Write-Host "phase test -- fixed goal, no controller: wrist +$TestMillimetres mm in model Z"
    Write-Host ('  ' + (Send 'ik.drive 0'))
    Write-Host ('  ' + (Send "ik.test 0 0 $TestMillimetres"))
    Write-Host ('  ' + (Send 'ik.mode 2'))
    Start-Sleep -Seconds 3
    $r = Send 'report'
    Write-Host ('  ikWrittenR=' + (Field $r 'ikWrittenR') + ' ikClamped=' + (Field $r 'ikClamped') + ' ikGoalMm=' + (Field $r 'ikGoalMm'))
    Write-Host '  ASK THE WEARER: did the right hand AND the weapon rise together?'
    Write-Host '    both      -> items 1-3 proven; the seam precedes the attachment update'
    Write-Host '    hand only -> the weapon is not bound to this rig wrist (check weaponBone above)'
    Write-Host '    neither   -> ikWrittenR must be climbing; if it is, the target/weight indices are wrong'
    Write-Host '  revert with: ik.test 0 0 0'
}

if ($Drive) {
    Write-Host ''
    Write-Host 'phase drive -- the controller owns the wrist through the engine IK'
    Write-Host ('  ' + (Send 'ik.test 0 0 0'))
    Write-Host ('  ' + (Send 'ik.hands 1'))
    Write-Host ('  ' + (Send 'ik.drive 1'))
    Write-Host ('  ' + (Send 'ik.mode 2'))
    Write-Host '  -- hold the right controller where the headset can see it --'
    Start-Sleep -Seconds 2
    Write-Host ('  ' + (Send 'ik.calibrate'))
    Start-Sleep -Seconds 3
    $r = Send 'report'
    Write-Host ('  ikWrittenR=' + (Field $r 'ikWrittenR') + ' ikNoPose=' + (Field $r 'ikNoPose') + ' ikClamped=' + (Field $r 'ikClamped') + ' ikCalR=' + (Field $r 'ikCalR') + ' ikGoalMm=' + (Field $r 'ikGoalMm'))
    Write-Host '  ASK THE WEARER: is the hand where the controller is, does the weapon come with it, does the elbow bend?'
    Write-Host '  aim.origin 1 is a controller-point diagnostic, not a muzzle/barrel calibration.'
    Write-Host '  fire a stationary test shot, then report: check muzzleSamples, muzzleFallback, muzzleAimGapMm and muzzleGripGapMm.'
    Write-Host '  re-equip/recenter/focus reset invalidates wrist calibration: issue ik.calibrate again.'
    Write-Host '  revert with: ik.mode 0'
}
