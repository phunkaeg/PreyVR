<#
.SYNOPSIS
    Launches Prey with an explicit XR child environment and a per-run state
    directory, then reports the PID to inject into.

.DESCRIPTION
    The pieces for an unattended run existed and were never joined. F-004 records
    a scripted direct launch that reached renderer initialisation and created the
    game window; its `SteamAppId=480490` is what defeats
    `SteamAPI_RestartAppIfNecessary`, which had silently killed every earlier
    direct launch. F-008 records `LoadLibraryW` injection through Frida working.
    This joins them.

    **The environment is built for the child, not set on this shell.** Setting
    `$env:` and restoring it afterwards leaves a window in which any other
    process inherits a redirected OpenXR runtime, and it does not survive a shell
    that exits early. `ProcessStartInfo.EnvironmentVariables` applies to exactly
    one process.

    **Every run gets its own state and trace directories.** Sharing
    `%LOCALAPPDATA%\xr-sim\preyvr` between runs is what let a previous session's
    capture be read as a new one; an audit found two state files claiming live
    FOCUSED sessions whose PIDs were long gone. A unique directory makes that
    impossible rather than guarded.

    **It does not inject.** No Frida CLI is installed here, so injection runs
    through the MCP tools against the PID this prints. The DLL's SHA-256 is
    recorded so what actually got injected can be checked against what was meant.

.PARAMETER DryRun
    Validate everything and print the plan without starting the game. Run this
    first: a launcher whose preflight has never executed is a launcher that has
    never been tested.

.EXAMPLE
    ./tools/Invoke-PreyVRLaunch.ps1 -DryRun
    ./tools/Invoke-PreyVRLaunch.ps1
#>
[CmdletBinding()]
param(
    [string]$GameExe = 'D:\SteamLibrary\steamapps\common\Prey\Binaries\Danielle\x64\Release\Prey.exe',
    [string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Prey',
    [string]$Dll = 'build\configure\Release\PreyVR.dll',
    [string]$XrSimRuntime = "$env:LOCALAPPDATA\xr-sim\runtime\xrsim-x64.json",
    [string]$XrTapeLayer = "$env:LOCALAPPDATA\xr-tape\layer-x64",
    [string]$RunRoot = "$env:LOCALAPPDATA\PreyVR\runs",
    [switch]$NoTape,
    [int]$TapeMaxFrames = 20000,
    [int]$ReadyTimeoutSec = 120,
    # Passed through to the game verbatim. CryEngine treats a `+`-prefixed
    # argument as a console command executed at startup, so `+map <level>` is
    # the documented way into a level without touching a menu. Whether Prey
    # kept that is a question this parameter exists to answer, not an assumption.
    [string]$ExtraArgs = '',
    # Leave the machine's own OpenXR runtime alone, for a real headset session.
    # Without this the launcher pins XR_RUNTIME_JSON to xr-sim, which is right
    # for unattended capture and wrong when someone is wearing a Quest.
    [switch]$Headset,
    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Default paths are repo-relative, not cwd-relative. This script is run from
# tools/ as often as from the root, and "mod DLL not found" from tools/ with a
# freshly built DLL cost a headset session its first ten minutes.
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($Dll)) { $Dll = Join-Path $repoRoot $Dll }

# --- preflight, each check named for the failure it prevents ------------------

foreach ($required in @($GameExe, $GameRoot, $XrSimRuntime)) {
    if (-not (Test-Path -LiteralPath $required)) {
        Write-Error "not found: $required"
        exit 1
    }
}
if (-not (Test-Path -LiteralPath $Dll)) {
    Write-Error "mod DLL not found: $Dll (build it first)"
    exit 1
}
$useTape = -not $NoTape
if ($useTape -and -not (Test-Path -LiteralPath $XrTapeLayer)) {
    Write-Warning "xr-tape layer not found at $XrTapeLayer - continuing without the recorder"
    $useTape = $false
}

# **Two Prey processes make every later observation ambiguous**: state.json, the
# tape header and the injection target would each pick one without saying which.
$existing = @(Get-Process -Name 'Prey' -ErrorAction SilentlyContinue)
if ($existing.Count -gt 0) {
    Write-Error ("Prey is already running (PID {0}). Close it: a second instance " -f ($existing[0].Id) +
                 'makes the PID, the state directory and the trace ambiguous.')
    exit 1
}

$dllFull = (Resolve-Path -LiteralPath $Dll).Path
$dllHash = (Get-FileHash -LiteralPath $dllFull -Algorithm SHA256).Hash
$dllSize = (Get-Item -LiteralPath $dllFull).Length

# **Record which build.** Older instructions in this repo point at
# build\headless\Release\PreyVR.dll, a different and much older artifact; a run
# that injects the wrong one produces findings about code that is not on disk.
Write-Host ("mod: {0}" -f $dllFull)
Write-Host ("     {0} bytes, SHA-256 {1}" -f $dllSize, $dllHash)

$runId = 'run-{0:yyyyMMdd-HHmmss}' -f (Get-Date)
$runDir = Join-Path $RunRoot $runId
$xrsimDir = Join-Path $runDir 'xrsim'
$tapeDir = Join-Path $runDir 'tape'
$logDir = Join-Path $runDir 'log'

# The child environment, entire. Anything not listed here the game inherits from
# this process, which is correct: only the XR selection and the mod's log
# location are ours to decide.
$childEnv = [ordered]@{
    # F-004: without this, Steam relaunches through its own path and the direct
    # child dies immediately, which reads as "the game refused to start".
    'SteamAppId'      = '480490'
    'XRSIM_DIR'       = $xrsimDir
    # **A file path, not a directory.** The mod resolves this straight to its log
    # file and derives the command channel from `parent_path()`, so handing it a
    # directory would put commands.txt one level too high and create a file
    # literally named after the folder. The channel then lands in `log\`
    # alongside the log, which is what gives the run its own commands.txt and
    # results.txt rather than sharing Documents\PreyVR with every other run.
    'PREYVR_LOG_PATH' = (Join-Path $logDir 'PreyVR.log')
}
if (-not $Headset) {
    # xr-sim, for unattended runs. Inserted after SteamAppId so the ordering of
    # the printed block still reads sensibly.
    $childEnv['XR_RUNTIME_JSON'] = (Resolve-Path -LiteralPath $XrSimRuntime).Path
} else {
    Write-Host 'headset mode: leaving XR_RUNTIME_JSON alone (machine runtime, e.g. VirtualDesktopXR)'
}
if ($useTape) {
    $childEnv['XR_API_LAYER_PATH'] = (Resolve-Path -LiteralPath $XrTapeLayer).Path
    $childEnv['XR_ENABLE_API_LAYERS'] = 'XR_APILAYER_XRTAPE_recorder'
    $childEnv['XRTAPE_DIR'] = $tapeDir
    # Bounded: an unbounded recorder on a session left running overnight fills
    # the disk and the trace becomes unreadable anyway.
    $childEnv['XRTAPE_MAX_FRAMES'] = "$TapeMaxFrames"
}

Write-Host ''
Write-Host "run: $runId"
Write-Host "  state:  $xrsimDir"
if ($useTape) { Write-Host "  tape:   $tapeDir" }
Write-Host ("  log:    {0}" -f (Join-Path $logDir 'PreyVR.log'))
Write-Host ''
Write-Host 'child environment:'
foreach ($key in $childEnv.Keys) { Write-Host ("  {0} = {1}" -f $key, $childEnv[$key]) }
Write-Host ''

if ($DryRun) {
    Write-Host 'DRY RUN - nothing launched. Preflight passed.'
    exit 0
}

foreach ($dir in @($runDir, $xrsimDir, $logDir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
if ($useTape) { New-Item -ItemType Directory -Path $tapeDir -Force | Out-Null }

# --- launch -------------------------------------------------------------------
#
# Game.log is appended to across runs, so the marker search below must only
# consider what this run wrote. Its length now is that boundary.
$gameLog = Join-Path $GameRoot 'Game.log'
$logOffset = 0
if (Test-Path -LiteralPath $gameLog) { $logOffset = (Get-Item -LiteralPath $gameLog).Length }

$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = (Resolve-Path -LiteralPath $GameExe).Path
$startInfo.WorkingDirectory = (Resolve-Path -LiteralPath $GameRoot).Path
# Required for EnvironmentVariables to apply at all; with ShellExecute the child
# would silently inherit this shell's environment instead.
$startInfo.UseShellExecute = $false
if ($ExtraArgs) {
    $startInfo.Arguments = $ExtraArgs
    Write-Host "arguments: $ExtraArgs"
}
foreach ($key in $childEnv.Keys) { $startInfo.EnvironmentVariables[$key] = $childEnv[$key] }

$process = [System.Diagnostics.Process]::Start($startInfo)
Write-Host ("launched PID {0}" -f $process.Id)

# **Verify the child survived as itself.** Steam can still replace a direct
# launch; if that happens the PID printed above is dead and the real game is a
# process this script never started. Reporting the wrong PID would send the
# injection at nothing.
Start-Sleep -Seconds 5
$pid_ = $process.Id
if ($process.HasExited) {
    Write-Warning ("the launched process exited (code {0}); looking for a relaunched instance" -f `
                   $process.ExitCode)
    $found = @(Get-Process -Name 'Prey' -ErrorAction SilentlyContinue)
    if ($found.Count -eq 0) {
        Write-Error 'the game exited and nothing replaced it. Check Game.log and error.log in the game root.'
        exit 1
    }
    $pid_ = $found[0].Id
    Write-Warning ("using relaunched PID {0} - note its environment is NOT the one above" -f $pid_)
    Write-Warning 'a relaunched instance did not inherit XRSIM_DIR; treat this run as unconfigured.'
}

# --- wait for the renderer ----------------------------------------------------
#
# Injecting before the renderer exists means the landmark gate runs against a
# half-initialised module. F-004's own evidence is this marker.
Write-Host 'waiting for renderer initialisation...'
$ready = $false
$deadline = (Get-Date).AddSeconds($ReadyTimeoutSec)
while ((Get-Date) -lt $deadline) {
    if (Test-Path -LiteralPath $gameLog) {
        $item = Get-Item -LiteralPath $gameLog
        $from = $logOffset
        if ($item.Length -lt $logOffset) { $from = 0 }   # the game truncated it
        $text = ''
        try {
            $stream = [System.IO.File]::Open($gameLog, 'Open', 'Read', 'ReadWrite')
            try {
                [void]$stream.Seek($from, 'Begin')
                $reader = New-Object System.IO.StreamReader($stream)
                $text = $reader.ReadToEnd()
            } finally { $stream.Dispose() }
        } catch { }
        if ($text -match 'Renderer initialization' -or $text -match "Creating window called 'Prey'") {
            $ready = $true
            break
        }
    }
    if ((Get-Process -Id $pid_ -ErrorAction SilentlyContinue) -eq $null) {
        Write-Error 'the game exited before the renderer came up. Check Game.log and error.log.'
        exit 1
    }
    Start-Sleep -Milliseconds 500
}
if (-not $ready) {
    Write-Warning "no renderer marker within $ReadyTimeoutSec s; the process is alive but may still be loading"
} else {
    Write-Host 'renderer up.'
}

# --- receipts -----------------------------------------------------------------

$manifest = [ordered]@{
    runId       = $runId
    startedUtc  = (Get-Date).ToUniversalTime().ToString('o')
    gameExe     = $startInfo.FileName
    gameRoot    = $startInfo.WorkingDirectory
    pid         = $pid_
    rendererUp  = $ready
    dll         = $dllFull
    dllSha256   = $dllHash
    dllBytes    = $dllSize
    xrsimDir    = $xrsimDir
    tapeDir     = $(if ($useTape) { $tapeDir } else { $null })
    logDir      = $logDir
    environment = $childEnv
}
$manifestPath = Join-Path $runDir 'run.json'
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host ''
Write-Host "PID:      $pid_"
Write-Host "manifest: $manifestPath"
Write-Host ''
Write-Host 'next: inject the DLL into that PID, then drive it through'
Write-Host ("  {0}\commands.txt" -f $logDir)
Write-Host 'and observe with'
Write-Host ("  ./tools/Invoke-PreyVRControllerSweep.ps1 -StateDir '{0}'" -f $xrsimDir)
