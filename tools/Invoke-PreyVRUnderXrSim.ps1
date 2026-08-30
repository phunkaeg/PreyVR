<#
.SYNOPSIS
    Runs a PreyVR executable against the xr-sim headset-free OpenXR runtime.

.DESCRIPTION
    xr-sim (D:\Dev Debug\xr-sim) is a deterministic OpenXR runtime that needs no
    headset. Prey is x64/D3D11, which is xr-sim's richest tier: full session,
    swapchain and frame lifecycle, plus per-layer PNG/JSON capture that selects
    the submitted imageArrayIndex, so a two-slice stereo texture captures the
    correct eye.

    This is a thin wrapper. It does not reimplement anything xr-sim already does
    -- it knows PreyVR's paths and defaults and delegates the rest, so the two
    projects cannot drift apart in the way a copied launcher would.

    **The runtime is selected per process.** xr-sim uses XR_RUNTIME_JSON, which
    the OpenXR loader checks ahead of the registry, so the machine-wide active
    runtime is untouched and a headset connected through VirtualDesktopXR keeps
    working while a test runs against the sim. Nothing here writes to the
    registry.

.PARAMETER Executable
    What to run. Defaults to the adapter probe, which is the cheapest end-to-end
    check that the loader, the runtime and our client agree.

.EXAMPLE
    ./tools/Invoke-PreyVRUnderXrSim.ps1
    ./tools/Invoke-PreyVRUnderXrSim.ps1 -Executable build/headless/Release/preyvr_xr_adapter_probe.exe
#>
[CmdletBinding()]
param(
    [string]$XrSimRoot = 'D:\Dev Debug\xr-sim',
    [string]$Executable = 'build/headless/Release/preyvr_xr_adapter_probe.exe',
    [string[]]$Arguments = @(),
    [ValidateSet('x64', 'x86')][string]$Architecture = 'x64',
    [string]$StateName = 'preyvr',
    [switch]$ShowState
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# --- preflight, each check named after the failure it prevents ---------------

$installScript = Join-Path $XrSimRoot 'tools\install-runtime.ps1'
if (-not (Test-Path -LiteralPath $installScript)) {
    throw "xr-sim not found at $XrSimRoot (expected tools\install-runtime.ps1). Pass -XrSimRoot."
}
if (-not (Test-Path -LiteralPath $Executable)) {
    throw "executable not found: $Executable"
}

# Prey is 64-bit. A wrong-bitness runtime is silently skipped by the loader,
# which would make a run quietly use the machine's real runtime while the
# transcript claims it used the sim -- the worst kind of false pass.
if ($Architecture -ne 'x64') {
    Write-Warning "Prey.exe is x64; running the $Architecture runtime is only meaningful for tooling experiments."
}

$install = & $installScript -Architecture $Architecture
$manifest = $install.Manifest
if (-not (Test-Path -LiteralPath $manifest)) {
    throw "xr-sim install did not produce a manifest at $manifest"
}

$stateDir = Join-Path $env:LOCALAPPDATA "xr-sim\$StateName"

# Saved and restored rather than set-and-forget: this shell may go on to do
# other things, and leaving XR_RUNTIME_JSON set would silently redirect them.
$savedRuntime = $env:XR_RUNTIME_JSON
$savedDir = $env:XRSIM_DIR
try {
    $env:XR_RUNTIME_JSON = $manifest
    $env:XRSIM_DIR = $stateDir

    Write-Output "preyvr_xrsim runtime=$manifest state=$stateDir arch=$Architecture"
    Write-Output "preyvr_xrsim executable=$Executable"
    Write-Output ''

    & $Executable @Arguments
    $exit = $LASTEXITCODE
}
finally {
    $env:XR_RUNTIME_JSON = $savedRuntime
    $env:XRSIM_DIR = $savedDir
}

Write-Output ''
Write-Output "preyvr_xrsim exit=$exit"

if ($ShowState) {
    $statePath = Join-Path $stateDir 'state.json'
    if (Test-Path -LiteralPath $statePath) {
        Write-Output ''
        Write-Output "--- $statePath ---"
        Get-Content -LiteralPath $statePath | Write-Output
    } else {
        Write-Warning "no state.json at $statePath - the client may not have created a session"
    }
}

exit $exit
