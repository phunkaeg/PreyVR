<#
.SYNOPSIS
    Answers the open HUD and reticle questions from a running game, without a headset.

.DESCRIPTION
    Everything left open by R-118 through R-121 is a question about what Prey
    actually does at the headset aspect: whether the native ScreenToFlash
    conversion agrees with our reconstruction, whether clearing `bMax` brings
    the clipped HUD edges into view, and how much of the rendered frustum the
    runtime can show.

    **None of that needs a headset on a head.** R-118 was measured exactly this
    way -- xr-sim driving the poses, the mod's own capture writing backbuffer
    dumps, and the dumps decoded and looked at. A wearer is needed to judge
    comfort and to confirm the trigger fires; a wearer is not needed to see
    whether a sprite moved or whether the HUD's left edge exists.

    This drives the command channel of an ALREADY RUNNING, already injected
    game. It does not launch Prey, does not change the machine's active OpenXR
    runtime, and does not touch the installed game. It restores the HUD
    constraint it changes, unconditionally.

    The paired captures are the point: two frames of the same view with only
    `bMax` different between them, so the comparison is controlled rather than
    two screenshots taken minutes apart and argued about afterwards.

.PARAMETER OutputDirectory
    Where PNGs and the transcript are written. Defaults to a timestamped folder
    under the current run's log directory, so evidence stays with its session.

.EXAMPLE
    ./tools/Invoke-PreyVRHudValidation.ps1
#>
[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [int]$MaxWidth = 900,
    [string]$ReleaseDirectory = 'D:\Dev Debug\PreyVR\build\configure\Release',
    [int]$TimeoutSeconds = 25
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# --- the channel -----------------------------------------------------------
# The mod polls commands.txt and writes results.txt in the newest run folder.
# Discovering it rather than being told it means this cannot be pointed at a
# stale session by accident.
$runsRoot = Join-Path $env:LOCALAPPDATA 'PreyVR\runs'
if (-not (Test-Path -LiteralPath $runsRoot)) {
    Write-Error "no PreyVR runs directory at $runsRoot -- has the mod ever run?"
    exit 1
}
$latest = Get-ChildItem -LiteralPath $runsRoot -Directory |
          Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($null -eq $latest) { Write-Error 'no run folders found'; exit 1 }

$logDir = Join-Path $latest.FullName 'log'
if (-not (Test-Path -LiteralPath $logDir)) {
    Write-Error "run folder $($latest.Name) has no log directory"
    exit 1
}
$commandFile = Join-Path $logDir 'commands.txt'
$resultFile = Join-Path $logDir 'results.txt'

if (-not $OutputDirectory) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $OutputDirectory = Join-Path $logDir "hud-validation-$stamp"
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$transcript = New-Object System.Collections.Generic.List[string]
function Write-Line([string]$text) {
    Write-Host $text
    $transcript.Add($text) | Out-Null
}

# A command is only answered when results.txt is rewritten. Comparing the write
# time against the value taken before the send is what separates a fresh answer
# from the previous command's -- the mistake that made an early sweep in this
# project read stale values and reach a confident wrong conclusion.
function Send-Command([string]$line) {
    $before = [datetime]::MinValue
    if (Test-Path -LiteralPath $resultFile) {
        $before = (Get-Item -LiteralPath $resultFile).LastWriteTime
    }
    Set-Content -LiteralPath $commandFile -Value $line -Encoding ASCII
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 200
        if (Test-Path -LiteralPath $resultFile) {
            $item = Get-Item -LiteralPath $resultFile
            if ($item.LastWriteTime -gt $before) {
                $text = Get-Content -LiteralPath $resultFile -Raw
                if ($text) { return $text.Trim() }
            }
        }
    }
    return '(no result within timeout)'
}

function Invoke-Step([string]$label, [string]$command) {
    $answer = Send-Command $command
    Write-Line ''
    Write-Line "--- $label"
    Write-Line "> $command"
    Write-Line $answer
    return $answer
}

# Take one frame and decode it. Old dumps for this tag are removed first so a
# stale file cannot stand in for a capture that never happened: an empty result
# must read as "no capture", never as the previous one.
function Get-Frame([int]$tag, [string]$name) {
    $pattern = Join-Path $ReleaseDirectory "frame-*-tag$tag.pvrframe"
    Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue | Remove-Item -Force
    Send-Command "capture $tag" | Out-Null
    Start-Sleep -Seconds 2
    Send-Command "capture $tag" | Out-Null
    Start-Sleep -Seconds 3

    $frame = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($null -eq $frame) {
        Write-Line "  capture FAILED for $name (tag $tag) -- no dump was written"
        return $null
    }

    $existing = @(Get-ChildItem -Path (Join-Path $OutputDirectory '*.png') -ErrorAction SilentlyContinue |
                  ForEach-Object { $_.FullName })
    & (Join-Path $PSScriptRoot 'Convert-FrameDump.ps1') `
        -Path $frame.FullName -OutputDirectory $OutputDirectory -MaxWidth $MaxWidth | Out-Null
    $produced = @(Get-ChildItem -Path (Join-Path $OutputDirectory '*.png') -ErrorAction SilentlyContinue |
                  Where-Object { $existing -notcontains $_.FullName })
    if ($produced.Count -eq 0) {
        Write-Line "  decode FAILED for $name"
        return $null
    }
    $target = Join-Path $OutputDirectory "$name.png"
    Move-Item -LiteralPath $produced[0].FullName -Destination $target -Force
    Write-Line "  captured $name -> $target"
    return $target
}

Write-Line 'PreyVR HUD validation'
Write-Line "run:    $($latest.Name)"
Write-Line "output: $OutputDirectory"

# --- 1. what the runtime wants against what we render -----------------------
Invoke-Step 'frustum coverage' 'xr.coverage' | Out-Null
Invoke-Step 'resolution chain' 'xr.resolution' | Out-Null

# --- 2. the two conversions, on the same input ------------------------------
# Off centre on X, then off centre on Y. The second matters most: every sample
# behind R-118 sat at y=0.5, exactly where the correct and incorrect models
# agree, so Y has never actually been measured by anything.
Invoke-Step 'probe, off-centre X' 'hud.probe 0.395 0.5' | Out-Null
Start-Sleep -Seconds 1
Invoke-Step 'probe result, off-centre X' 'hud.probe' | Out-Null
Invoke-Step 'probe, off-centre Y' 'hud.probe 0.5 0.395' | Out-Null
Start-Sleep -Seconds 1
Invoke-Step 'probe result, off-centre Y' 'hud.probe' | Out-Null

# --- 3. the HUD fit, as a controlled pair -----------------------------------
# Cover first, so the baseline is what the game normally does rather than a
# state this script put it in.
Write-Line ''
Write-Line '--- HUD fit comparison'
$before = Get-Frame -tag 21 -name 'hud-cover-bmax1'
Invoke-Step 'fit the canvas inside the frame' 'hud.fit 1' | Out-Null
Start-Sleep -Seconds 2
Invoke-Step 'constraints after the change' 'hud.probe 0.5 0.5' | Out-Null
Start-Sleep -Seconds 1
Invoke-Step 'read them back' 'hud.probe' | Out-Null
$after = Get-Frame -tag 22 -name 'hud-fit-bmax0'

# Restored unconditionally. Leaving the wearer's HUD in a state this script
# chose would be changing the game rather than measuring it.
Invoke-Step 'restore the engine default' 'hud.fit 0' | Out-Null

# --- 4. how to read it ------------------------------------------------------
Write-Line ''
Write-Line '=== what to read ==='
Write-Line 'stage0X / stage1X against oursX -- the engine conversion against the R-118 model.'
Write-Line '  Agreeing: the reconstruction was right, and aim.reticlecanvas 2 can replace it.'
Write-Line '  Differing: the native value is the one to dispatch, and the model is wrong.'
Write-Line 'cMax=1 confirms the cover fit at its source rather than by inference.'
Write-Line 'leftUsed / leftCovered -- pixels are reclaimable only while Covered is 1.0.'
if ($null -ne $before -and $null -ne $after) {
    Write-Line 'Compare hud-cover-bmax1.png against hud-fit-bmax0.png:'
    Write-Line '  the fitted frame should show HUD content the cover frame clips off each side.'
} else {
    Write-Line 'One or both captures failed -- the HUD comparison is NOT available.'
}

$transcriptPath = Join-Path $OutputDirectory 'transcript.txt'
Set-Content -LiteralPath $transcriptPath -Value ($transcript -join [Environment]::NewLine) -Encoding UTF8
Write-Host ''
Write-Host "transcript: $transcriptPath"
