[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$BuildDirectory = '',
    [string]$GameReleasePath = 'D:\SteamLibrary\steamapps\common\Prey\Binaries\Danielle\x64\Release',
    [switch]$NoFresh,
    [switch]$RequireGameBaseline
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repoRoot 'build\headless'
}

$configureArguments = @(
    '-S', $repoRoot,
    '-B', $BuildDirectory,
    "-DPREYVR_GAME_RELEASE_DIR=$GameReleasePath",
    "-DPREYVR_REQUIRE_GAME_BASELINE=$($RequireGameBaseline.IsPresent.ToString().ToUpperInvariant())"
)
if (-not $NoFresh) {
    $configureArguments = @('--fresh') + $configureArguments
}

if (-not (Test-Path -LiteralPath $GameReleasePath -PathType Container)) {
    $mode = if ($RequireGameBaseline) { 'required (test will fail)' } else { 'optional (doctor will warn and skip)' }
    Write-Host "Prey game baseline is unavailable: $GameReleasePath; mode=$mode"
}

& cmake @configureArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& cmake --build $BuildDirectory --config $Configuration
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
exit $LASTEXITCODE
