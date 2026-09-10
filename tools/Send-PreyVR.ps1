[CmdletBinding()]
param(
    # Everything after the switches is treated as one command, so
    #   Tune Prey VR.cmd ui.scale 130
    # works as well as the interactive prompt.
    # **Position=0 is load-bearing.** A ValueFromRemainingArguments parameter is
    # skipped when PowerShell assigns positions, so without it `Send-PreyVR
    # vr.status` bound "vr.status" to -RunDir and died resolving it as a
    # directory. Declaring one position also makes the other two name-only,
    # which is what we want -- nothing else here should be positional.
    [Parameter(Position=0,ValueFromRemainingArguments=$true)][string[]]$Command,
    [string]$RunDir='',
    [int]$TimeoutSeconds=25
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

# **Prey 2017 ships no user console**, so every runtime knob in this mod is
# reached through a file the DLL polls: it reads and truncates commands.txt in
# the run directory, then writes the reply to results.txt. That is the whole
# channel -- no console, no overlay, no keybind needed.
#
# This script exists because the alternative instruction was "open Notepad on
# a path under AppData with a timestamp in it", which nobody is doing between
# rounds with a headset on their face.

try {
    if (-not $RunDir) {
        # Newest run that actually has a log, rather than newest directory: the
        # launcher creates the directory before the game writes anything, so a
        # failed start would otherwise shadow the session that is really up.
        $root=Join-Path $env:LOCALAPPDATA 'PreyVR\runs'
        if (!(Test-Path -LiteralPath $root)) {throw "No runs yet: $root does not exist. Start the game with the launcher first."}
        $log=Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue |
             ForEach-Object {Get-ChildItem -LiteralPath $_.FullName -Filter 'PreyVR.log' -Recurse -File -ErrorAction SilentlyContinue} |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (!$log) {throw "No PreyVR.log under $root. Start the game with the launcher first."}
        $RunDir=$log.DirectoryName
    }
    $RunDir=(Resolve-Path -LiteralPath $RunDir).Path
    $cmd=Join-Path $RunDir 'commands.txt'
    $res=Join-Path $RunDir 'results.txt'

    # A stale run directory answers nothing and looks like a hang, so say so up
    # front rather than after a 25-second timeout.
    if (!(Get-Process -Name 'Prey' -ErrorAction SilentlyContinue)) {
        Write-Host 'Prey is not running -- start it with the launcher first.' -ForegroundColor Yellow
    }

    function Send-Cmd($line) {
        $before=[datetime]::MinValue
        if (Test-Path -LiteralPath $res) {$before=(Get-Item -LiteralPath $res).LastWriteTime}
        Set-Content -LiteralPath $cmd -Value $line -Encoding ASCII
        $deadline=(Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 200
            if (Test-Path -LiteralPath $res) {
                $item=Get-Item -LiteralPath $res
                if ($item.LastWriteTime -gt $before) {
                    $text=Get-Content -LiteralPath $res -Raw
                    if ($text) {return $text.Trim()}
                }
            }
        }
        return '(no reply -- is the mod loaded and running?)'
    }

    # Only verbs that are wired and were exercised in a headset. The channel
    # accepts many more; the full set is in docs/, and an unknown verb answers
    # with an error rather than doing something surprising.
    $help=@(
        'ui.scale 130          HUD/menu panel size, 20..200 percent (100 = default)'
        'ui.curve 35           panel curvature in degrees, 0 = flat'
        'ui.pointer 1          laser hand: 0 left, 1 right, 2 buttons only'
        'hud.layer 1           draw the HUD as its own layer (0 = in-world)'
        'vr.status             where VR setup got to'
        'vr.recenter           re-centre the view (F12 and both grips do this too)'
        'vr.enable / vr.disable'
        'ik.anchor 1           1 = camera-anchored hands (0 restores the crosstalk, for A/B)'
        'move.turn 1           snap turn on the right stick'
        'report                a block of counters'
    )

    if ($Command -and $Command.Count) {
        $reply=Send-Cmd ($Command -join ' ')
        Write-Output $reply
        # Non-zero when nothing answered, so a script calling this cannot read a
        # dead channel as a successful send -- the same mistake the startup
        # script made when it reported an unavailable runtime as ok.
        if ($reply -like '(no reply*') { exit 2 }
        exit 0
    }

    Write-Host "Prey VR command channel -- $RunDir"
    Write-Host 'Type a command, "help" for the common ones, or "quit".'
    Write-Host ''
    while ($true) {
        $line=Read-Host 'preyvr'
        if (!$line) {continue}
        $line=$line.Trim()
        if ($line -in @('quit','exit','q')) {break}
        if ($line -eq 'help') {$help | ForEach-Object {Write-Host "  $_"}; continue}
        Write-Host ('  ' + (Send-Cmd $line))
    }
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    # Hold the window open only for the double-click case. With arguments this is
    # a one-shot call, and blocking on Read-Host there hangs whatever invoked it.
    if (-not ($Command -and $Command.Count)) { Read-Host 'Press Enter to close' | Out-Null }
    exit 1
}
