[CmdletBinding()]
param(
    [string]$GameReleasePath = 'D:\SteamLibrary\steamapps\common\Prey\Binaries\Danielle\x64\Release',
    [string]$EngineMapProbe = '',
    [switch]$AllowMissingGame
)

$ErrorActionPreference = 'Stop'
$passed = 0
$warnings = 0
$failures = 0

function Write-DoctorResult {
    param([ValidateSet('PASS', 'WARN', 'FAIL')][string]$Status, [string]$Message)
    switch ($Status) {
        'PASS' { $script:passed++ }
        'WARN' { $script:warnings++ }
        'FAIL' { $script:failures++ }
    }
    Write-Host "${Status}: $Message"
}

function Get-PeMachine {
    param([string]$Path)
    $stream = [System.IO.File]::Open(
        $Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
    try {
        if ($stream.Length -lt 0x40) { return $null }
        $reader = [System.IO.BinaryReader]::new($stream)
        $stream.Position = 0x3c
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or $peOffset + 6 -gt $stream.Length) { return $null }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { return $null }
        return $reader.ReadUInt16()
    } finally {
        $stream.Dispose()
    }
}

$expected = @(
    @{
        Name = 'Prey.exe'
        Hash = 'F179987F9786C57F9394A93B1009FC3AED3001E629C5B0B6CFF11882C55199F1'
    },
    @{
        Name = 'PreyDll.dll'
        Hash = '7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7'
    }
)

$gameAvailable = Test-Path -LiteralPath $GameReleasePath -PathType Container
if (-not $gameAvailable) {
    if ($AllowMissingGame) {
        Write-DoctorResult WARN "game baseline unavailable; skipped read-only module checks: $GameReleasePath"
    } else {
        Write-DoctorResult FAIL "game release directory is missing: $GameReleasePath"
    }
}

if ($gameAvailable) {
    foreach ($entry in $expected) {
        $path = Join-Path $GameReleasePath $entry.Name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            Write-DoctorResult FAIL "missing target module: $path"
            continue
        }

        $machine = Get-PeMachine $path
        if ($machine -eq 0x8664) {
            Write-DoctorResult PASS "$($entry.Name) is x86-64/PE32+"
        } else {
            Write-DoctorResult FAIL "$($entry.Name) has unexpected PE machine 0x$('{0:X4}' -f $machine)"
        }

        $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ($actualHash -eq $entry.Hash) {
            Write-DoctorResult PASS "$($entry.Name) SHA-256 matches the research baseline"
        } else {
            Write-DoctorResult FAIL "$($entry.Name) SHA-256 does not match the research baseline"
        }
    }

    $preyDllPath = Join-Path $GameReleasePath 'PreyDll.dll'
    if (Test-Path -LiteralPath $preyDllPath -PathType Leaf) {
        if ([string]::IsNullOrWhiteSpace($EngineMapProbe) -or
            -not (Test-Path -LiteralPath $EngineMapProbe -PathType Leaf)) {
            Write-DoctorResult FAIL 'engine-map probe is missing; landmark validation was not run'
        } else {
            $probeOutput = @(& $EngineMapProbe $preyDllPath 2>&1)
            $probeExit = $LASTEXITCODE
            $summary = @($probeOutput | Where-Object {
                $_ -match 'engine_map_probe result=(pass|failed)(?: landmarks=([0-9]+))?'
            })
            if ($probeExit -eq 0 -and $summary.Count -gt 0 -and
                $summary[-1] -match 'result=pass landmarks=([0-9]+)') {
                Write-DoctorResult PASS "PreyDll.dll matches all $($Matches[1]) compiled engine landmarks"
            } else {
                $probeOutput | ForEach-Object { Write-Host "  $_" }
                Write-DoctorResult FAIL "compiled engine-landmark validation failed (exit=$probeExit)"
            }
        }
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
if (Test-Path -LiteralPath (Join-Path $repoRoot 'regenny\PreyVR.genny') -PathType Leaf) {
    Write-DoctorResult PASS 'ReGenny type notebook is present'
} else {
    Write-DoctorResult FAIL 'ReGenny type notebook is missing'
}

$graphify = Get-Command graphify -ErrorAction SilentlyContinue
if ($null -ne $graphify) {
    Write-DoctorResult PASS "Graphify is available at $($graphify.Source)"
} else {
    Write-DoctorResult WARN 'Graphify is not on PATH; code tests remain available'
}

Write-Host "SUMMARY: pass=$passed warn=$warnings fail=$failures"
if ($failures -ne 0) { exit 1 }
