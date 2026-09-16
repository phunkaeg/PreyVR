# Run without -Publish to prepare and inspect the ZIP locally.
# Run with -Publish to also commit the allowlisted fix, push its branch/tag,
# and create the public GitHub prerelease. No native DLL is rebuilt here.
[CmdletBinding()]
param([switch]$Publish)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {throw "$Program failed with exit code $LASTEXITCODE"}
}
$repoRoot=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$repo='phunkaeg/PreyVR'
$baseTag='v0.5.0-preview'
$baseCommit='1ac2d6103d97f1321f991c46f2ccae8bbae6a115'
$tag='v0.5.1-preview'
$branch='codex/release-0.5.1'
$gh=(Get-Command gh -ErrorAction Stop).Source
$git=(Get-Command git -ErrorAction Stop).Source
# Public release preparation needs no authentication preflight. For publishing,
# use an actual API request so a network timeout is not labelled "invalid token"
# by gh auth status. Print only the account name, never credentials.
if ($Publish) {Invoke-Checked $gh @('api','user','--jq','.login')}

# Never include the current dirty native implementation in this release.
$fixFiles=@('tools/Start-PreyVR.ps1','docs/PLAYER-GUIDE.md',
    'docs/PLAYER-GUIDE-HOLOGRAM.md','tests/LauncherResolution.Tests.ps1',
    'docs/LAUNCHER-RESOLUTION-FIX-2026-09-13.md')
foreach ($relative in $fixFiles) {
    if (!(Test-Path -LiteralPath (Join-Path $repoRoot $relative))) {throw "Missing fix file: $relative"}
}
$runRoot=Join-Path $repoRoot ('build/release-0.5.1/'+[guid]::NewGuid().ToString('N'))
$download=Join-Path $runRoot 'download'
$unpacked=Join-Path $runRoot 'upstream'
[void](New-Item -ItemType Directory -Path $download -Force)
Invoke-Checked $gh @('release','download',$baseTag,'--repo',$repo,'--pattern','PreyVR-0.5.0-preview.zip','--dir',$download)
$baseZip=Join-Path $download 'PreyVR-0.5.0-preview.zip'
$expectedZip='d90224b16bd2169c6e6ca3632c2a390fb16b5459d7ddb5d34a7ff90aedff3fec'
if ((Get-FileHash -LiteralPath $baseZip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedZip) {
    throw 'Published v0.5.0 ZIP differs from the verified release asset. Review before repackaging.'
}
Expand-Archive -LiteralPath $baseZip -DestinationPath $unpacked
$launchers=@(Get-ChildItem -LiteralPath $unpacked -Recurse -File -Filter 'Start-PreyVR.ps1')
if ($launchers.Count -ne 1) {throw 'Expected exactly one player package in the upstream ZIP'}
$upstreamPackage=$launchers[0].Directory.FullName
$package=Join-Path $runRoot 'PreyVR-0.5.1-preview'
[void](New-Item -ItemType Directory -Path $package)
Get-ChildItem -LiteralPath $upstreamPackage -Force | Copy-Item -Destination $package -Recurse
if (Test-Path -LiteralPath (Join-Path $package 'PreyVR.json')) {
    throw 'Upstream package contains saved player settings; review them before publishing.'
}
$binaries=@('PreyVR.dll','openxr_loader.dll','preyvr_injector.exe')
$binaryHashes=@{}
foreach ($name in $binaries) {$binaryHashes[$name]=(Get-FileHash -LiteralPath (Join-Path $package $name)).Hash}
Copy-Item -LiteralPath (Join-Path $repoRoot 'tools/Start-PreyVR.ps1') -Destination (Join-Path $package 'Start-PreyVR.ps1') -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs/PLAYER-GUIDE-HOLOGRAM.md') -Destination (Join-Path $package 'README.md') -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs/LAUNCHER-RESOLUTION-FIX-2026-09-13.md') -Destination (Join-Path $package 'RESOLUTION-FIX.md')

# Exercise the actual launcher configuration code with the shell used by the
# shipped CMD wrapper. Failure prevents publication.
$windowsPowerShell=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
Invoke-Checked $windowsPowerShell @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $repoRoot 'tests/LauncherResolution.Tests.ps1'))
foreach ($name in $binaries) {
    if ((Get-FileHash -LiteralPath (Join-Path $package $name)).Hash -ne $binaryHashes[$name]) {throw "Unexpected binary change: $name"}
}
$manifestFiles=@(Get-ChildItem -LiteralPath $package -Recurse -File | Where-Object Name -ne 'manifest.json' | ForEach-Object {
    @{path=$_.FullName.Substring($package.Length+1).Replace('\','/');bytes=$_.Length;
      sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
@{version=$tag;basedOn=$baseTag;nativeBinariesUnchanged=$true;
  supportedPreyDll='7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7';
  files=$manifestFiles} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $package 'manifest.json') -Encoding UTF8
$zip=Join-Path $runRoot 'PreyVR-0.5.1-preview.zip'
Compress-Archive -LiteralPath $package -DestinationPath $zip
$verify=Join-Path $runRoot 'verify'
Expand-Archive -LiteralPath $zip -DestinationPath $verify
$verifyPackage=Join-Path $verify 'PreyVR-0.5.1-preview'
foreach ($entry in $manifestFiles) {
    if ((Get-FileHash -LiteralPath (Join-Path $verifyPackage $entry.path)).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "ZIP verification failed: $($entry.path)"
    }
}
$notes=Join-Path $runRoot 'release-notes.md'
@'
Corrects the widescreen launch default that can leave black borders around the gameplay view in VR.

- New installations default to 2016x2160 per eye, using the same aspect as the previously tested 2688x2880 headset setup at lower pixel cost.
- Old unversioned 2560x1440 configurations migrate once, with a backup. Custom resolutions and other settings are preserved.
- Explicit resolution overrides require both dimensions. For greater detail, select 2688x2880.
- Native binaries are unchanged from v0.5.0-preview. Experimental stereo-inventory changes are not included.

Download the complete ZIP and extract it into your existing player-package folder with Prey closed. Keep your PreyVR.json; the launcher handles migration. Then use Start Prey VR.cmd normally.

Launcher configuration tests pass on Windows PowerShell 5.1 and PowerShell 7. This fixes the default-resolution regression, but headset-specific FOV coverage and the reported border still need tester confirmation. Menus can retain unused space within their floating panel.
'@ | Set-Content -LiteralPath $notes -Encoding UTF8
Write-Host "Prepared ZIP: $zip"
Write-Host "Release notes: $notes"
if (!$Publish) {
    Write-Host 'Preparation only. Nothing committed, pushed or published. Run again with -Publish to publish a fresh verified package.'
    return
}

# Publish an isolated source commit. Existing branches, worktrees and local
# experimental edits are never staged by this script.
Invoke-Checked $git @('-C',$repoRoot,'fetch','origin',('refs/tags/'+$baseTag))
$resolved=& $git -C $repoRoot rev-parse 'FETCH_HEAD^{commit}'
if ($LASTEXITCODE -ne 0 -or $resolved.Trim() -ne $baseCommit) {throw 'Unexpected baseline tag target'}
$remoteTag=& $git -C $repoRoot ls-remote --tags origin ('refs/tags/'+$tag)
if ($LASTEXITCODE -ne 0) {throw 'Could not check release tag'}
if ($remoteTag) {throw "$tag already exists; do not overwrite an existing release"}
$source=Join-Path $runRoot 'source'
Invoke-Checked $git @('-C',$repoRoot,'worktree','add','-b',$branch,$source,$baseCommit)
foreach ($relative in $fixFiles) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $relative) -Destination (Join-Path $source $relative) -Force
}
Invoke-Checked $git (@('-C',$source,'add','--')+$fixFiles)
Invoke-Checked $git @('-C',$source,'diff','--cached','--check')
Invoke-Checked $git @('-C',$source,'commit','-m','fix: restore headset-oriented launch aspect and migrate legacy defaults')
Invoke-Checked $git @('-C',$source,'tag','-a',$tag,'-m','PreyVR 0.5.1 preview: launcher resolution fix')
Invoke-Checked $git @('-C',$source,'push','--atomic','origin',('HEAD:refs/heads/'+$branch),('refs/tags/'+$tag))
Invoke-Checked $gh @('release','create',$tag,$zip,'--repo',$repo,'--verify-tag','--title','PreyVR 0.5.1 Preview','--notes-file',$notes,'--prerelease')
Invoke-Checked $gh @('release','view',$tag,'--repo',$repo,'--json','url,assets')
Write-Host "Published $tag. Source fix is on $branch; merge that branch into main for future releases."
