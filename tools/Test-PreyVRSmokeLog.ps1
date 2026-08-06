[CmdletBinding()]
param(
    [string]$LogPath = '',
    [ValidateSet('verified', 'unsupported')]
    [string]$ExpectedStatus = 'verified',
    [ValidateRange(0, 1000)]
    [int]$ExpectedLandmarkCount = 0,
    [string]$ExpectedDllSha256 = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'PreyVR\PreyVR.log'
}
$LogPath = [System.IO.Path]::GetFullPath($LogPath)
if (-not (Test-Path -LiteralPath $LogPath -PathType Leaf)) {
    throw "PreyVR smoke log not found: $LogPath"
}

$lines = @(Get-Content -LiteralPath $LogPath)
$resultLines = @($lines | Where-Object { $_ -match 'preyvr_smoke_result status=([a-z_]+)' })
if ($resultLines.Count -eq 0) {
    throw "No preyvr_smoke_result line found in: $LogPath"
}

$resultLine = $resultLines[-1]
$null = $resultLine -match 'preyvr_smoke_result status=([a-z_]+)'
$actualStatus = $Matches[1]
$landmarkLines = @($lines | Where-Object { $_ -match 'preyvr_landmark ' })
$matchedLandmarks = @($landmarkLines | Where-Object { $_ -match ' status=match(?:\s|$)' }).Count
$startLines = @($lines | Where-Object { $_ -match 'preyvr_smoke_start ' })
if ($startLines.Count -eq 0 -or
    $startLines[-1] -notmatch 'dllSha256=([0-9A-Fa-f]{64})') {
    throw 'Latest smoke run has no valid DLL SHA-256 identity'
}
$actualDllSha256 = $Matches[1].ToUpperInvariant()

if ($actualStatus -ne $ExpectedStatus) {
    throw "Smoke status mismatch: expected=$ExpectedStatus actual=$actualStatus"
}
if ($ExpectedLandmarkCount -gt 0 -and $landmarkLines.Count -ne $ExpectedLandmarkCount) {
    throw "Landmark count mismatch: expected=$ExpectedLandmarkCount actual=$($landmarkLines.Count)"
}
if (-not [string]::IsNullOrWhiteSpace($ExpectedDllSha256) -and
    $actualDllSha256 -ne $ExpectedDllSha256.ToUpperInvariant()) {
    throw "DLL SHA-256 mismatch: expected=$ExpectedDllSha256 actual=$actualDllSha256"
}
if ($actualStatus -eq 'verified') {
    if ($resultLine -notmatch ' landmarks=([0-9]+)(?:\s|$)') {
        throw 'Verified result does not declare its landmark count'
    }
    $declaredLandmarks = [int]$Matches[1]
    if ($declaredLandmarks -le 0 -or $landmarkLines.Count -ne $declaredLandmarks -or
        $matchedLandmarks -ne $declaredLandmarks) {
        throw "Verified smoke log is internally inconsistent: declared=$declaredLandmarks lines=$($landmarkLines.Count) matched=$matchedLandmarks"
    }
    if ($ExpectedLandmarkCount -gt 0 -and $declaredLandmarks -ne $ExpectedLandmarkCount) {
        throw "Verified landmark count mismatch: expected=$ExpectedLandmarkCount declared=$declaredLandmarks"
    }
}

Write-Output "smoke_log_check result=pass status=$actualStatus landmarks=$($landmarkLines.Count) matched=$matchedLandmarks dllSha256=$actualDllSha256 path=`"$LogPath`""
