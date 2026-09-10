[CmdletBinding()]
param(
    [string]$BuildDir=(Join-Path $PSScriptRoot '../build/headless/Release'),
    [string]$OutputDir=(Join-Path $PSScriptRoot '../build/packages/PreyVR-hologram-preview-20260910'),
    [string]$GuidePath=(Join-Path $PSScriptRoot '../docs/PLAYER-GUIDE-HOLOGRAM.md'),
    [string]$ValidationPath=(Join-Path $PSScriptRoot '../docs/HOLOGRAM-VALIDATION.md')
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$destination=[System.IO.Path]::GetFullPath($OutputDir)
if (!(Test-Path -LiteralPath $destination)) {[void](New-Item -ItemType Directory -Path $destination)}
# Never overwrite a frozen player package or its settings.
if (@(Get-ChildItem -LiteralPath $destination -Force).Count) {throw 'Choose an empty output directory.'}
foreach($name in @('PreyVR.dll','openxr_loader.dll','preyvr_injector.exe')) {
    Copy-Item -LiteralPath (Join-Path $BuildDir $name) -Destination $destination
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Start-PreyVR.ps1'),(Join-Path $PSScriptRoot 'Start Prey VR.cmd') -Destination $destination
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
