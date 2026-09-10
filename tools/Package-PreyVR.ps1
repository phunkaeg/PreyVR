[CmdletBinding()]
param(
    [string]$BuildDir='',
    [string]$OutputDir='',
    [string]$GuidePath='',
    [string]$ValidationPath=''
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

# **`$PSScriptRoot` must not be a param default here.** Under Windows PowerShell
# 5.1, a script with [CmdletBinding()] invoked as `powershell.exe -File`
# evaluates param defaults in a scope where $PSScriptRoot is still empty --
# while $PSScriptRoot in the body is correct. PowerShell 7 does not do this, so
# the bug is invisible to anyone testing with pwsh.
#
# The shipped .cmd wrapper uses powershell.exe, so EVERY double-click hit it:
# $PackageDir came out empty and the first Join-Path threw "Cannot bind argument
# to parameter 'Path' because it is an empty string" before anything launched.
# Resolved in the body instead, which behaves identically in both versions.
$here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $PSCommandPath }
if (-not $BuildDir)       { $BuildDir = Join-Path $here '../build/headless/Release' }
if (-not $OutputDir)      { $OutputDir = Join-Path $here '../build/packages/PreyVR-hologram-preview-20260910' }
if (-not $GuidePath)      { $GuidePath = Join-Path $here '../docs/PLAYER-GUIDE-HOLOGRAM.md' }
if (-not $ValidationPath) { $ValidationPath = Join-Path $here '../docs/HOLOGRAM-VALIDATION.md' }
# **Refuse while Prey holds the binaries.** A package cannot be regenerated
# under a running game: it has PreyVR.dll and openxr_loader.dll open, so they
# survive a delete and everything around them does not -- which strips the
# folder to two orphaned DLLs and takes the launcher and the wearer's settings
# with it. That happened twice in one session, both times because the operator
# ran a delete first and checked afterwards. Checked here instead, where it
# cannot be forgotten, and before anything is removed.
$holding=@(Get-Process -Name 'Prey' -ErrorAction SilentlyContinue)
if ($holding.Count) {
    throw ('Close Prey first (PID {0}) -- it has the package DLLs open, and packaging over them destroys the folder.' -f
        (($holding | Select-Object -ExpandProperty Id) -join ', '))
}
$root=(Resolve-Path (Join-Path $here '..')).Path
$destination=[System.IO.Path]::GetFullPath($OutputDir)
if (!(Test-Path -LiteralPath $destination)) {[void](New-Item -ItemType Directory -Path $destination)}
# Never overwrite a frozen player package or its settings.
if (@(Get-ChildItem -LiteralPath $destination -Force).Count) {throw 'Choose an empty output directory.'}
foreach($name in @('PreyVR.dll','openxr_loader.dll','preyvr_injector.exe')) {
    Copy-Item -LiteralPath (Join-Path $BuildDir $name) -Destination $destination
}
# Ships the tuner too: Prey has no user console, so the command channel is the
# only way to change anything at runtime, and telling a wearer to hand-edit a
# timestamped path under AppData is not a usable instruction.
foreach($script in @('Start-PreyVR.ps1','Start Prey VR.cmd','Send-PreyVR.ps1','Tune Prey VR.cmd')) {
    Copy-Item -LiteralPath (Join-Path $here $script) -Destination $destination
}
Copy-Item -LiteralPath $GuidePath -Destination (Join-Path $destination 'README.md')
Copy-Item -LiteralPath $ValidationPath -Destination (Join-Path $destination 'VALIDATION.md')
$licenses=Join-Path $destination 'licenses'
[void](New-Item -ItemType Directory -Path $licenses)
Copy-Item -LiteralPath (Join-Path $root 'build/headless/_deps/minhook-src/LICENSE.txt') -Destination (Join-Path $licenses 'MinHook.txt')
Copy-Item -LiteralPath (Join-Path $root 'build/headless/_deps/openxr-src/LICENSE') -Destination (Join-Path $licenses 'OpenXR.txt')
$files=@(Get-ChildItem -LiteralPath $destination -Recurse -File | ForEach-Object {
    @{path=$_.FullName.Substring($destination.Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
@{built=(Get-Date).ToUniversalTime().ToString('o');supportedPreyDll='7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7';files=$files} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding UTF8
$zip="$destination.zip"
if(Test-Path -LiteralPath $zip){throw "Archive already exists: $zip"}
Compress-Archive -LiteralPath $destination -DestinationPath $zip
Write-Output "Player package: $destination"
Write-Output "Archive: $zip"
