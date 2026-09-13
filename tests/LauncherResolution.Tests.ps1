[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$scriptPath=Join-Path $PSScriptRoot '../tools/Start-PreyVR.ps1'
$tokens=$null;$parseErrors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile($scriptPath,[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count) {throw ($parseErrors | Out-String)}
# Load only the two configuration functions, never the launcher body.
foreach ($name in @('Resolve-PreyVRRenderSize','Save-PreyVRConfiguration')) {
    $function=$ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true)
    if (!$function) {throw "Missing $name"}
    Invoke-Expression $function.Extent.Text
}
function Assert-Size($result,$width,$height,$migration) {
    if ($result.Width -ne $width -or $result.Height -ne $height -or $result.MigrateLegacy -ne $migration) {
        throw "Unexpected render selection: $($result | ConvertTo-Json -Compress)"
    }
}
$defaultWidth=[int]$ast.ParamBlock.Parameters.Where({$_.Name.VariablePath.UserPath -eq 'Width'})[0].DefaultValue.Value
$defaultHeight=[int]$ast.ParamBlock.Parameters.Where({$_.Name.VariablePath.UserPath -eq 'Height'})[0].DefaultValue.Value
Assert-Size (Resolve-PreyVRRenderSize $null $defaultWidth $defaultHeight $false $false) 2016 2160 $false
$legacy=[pscustomobject]@{Width=2560;Height=1440;UiScalePercent=137;ExtensionSetting='retain me'}
Assert-Size (Resolve-PreyVRRenderSize $legacy $defaultWidth $defaultHeight $false $false) 2016 2160 $true
Assert-Size (Resolve-PreyVRRenderSize $legacy 2560 1440 $true $true) 2560 1440 $false
Assert-Size (Resolve-PreyVRRenderSize ([pscustomobject]@{Width=2688;Height=2880}) $defaultWidth $defaultHeight $false $false) 2688 2880 $false
Assert-Size (Resolve-PreyVRRenderSize ([pscustomobject]@{Width=1920;Height=1080}) $defaultWidth $defaultHeight $false $false) 1920 1080 $false
Assert-Size (Resolve-PreyVRRenderSize ([pscustomobject]@{Width=2560;Height=1440;ResolutionDefaultsVersion=1}) $defaultWidth $defaultHeight $false $false) 2560 1440 $false
foreach ($argsSet in @(@($null,2688,2160,$true,$false),@($null,2016,2880,$false,$true),@($null,0,2160,$true,$true))) {
    $rejected=$false
    try {$null=Resolve-PreyVRRenderSize @argsSet} catch {$rejected=$true}
    if (!$rejected) {throw 'Invalid or partial size was accepted'}
}
$testDir=Join-Path $PSScriptRoot ('../build/launcher-resolution-tests/'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $testDir -Force)
$config=Join-Path $testDir 'PreyVR.json'
$legacy | ConvertTo-Json | Set-Content -LiteralPath $config -Encoding UTF8
$before=(Get-FileHash -LiteralPath $config).Hash
Save-PreyVRConfiguration $config $legacy @{Width=2016;Height=2160} $true
$backups=@(Get-ChildItem -LiteralPath $testDir -Filter '*.bak')
if ($backups.Count -ne 1 -or (Get-FileHash -LiteralPath $backups[0].FullName).Hash -ne $before) {throw 'Original config backup missing or changed'}
$saved=Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
if ($saved.UiScalePercent -ne 137 -or $saved.ExtensionSetting -ne 'retain me' -or $saved.ResolutionDefaultsVersion -ne 1) {throw 'Migration lost unrelated configuration'}
Assert-Size (Resolve-PreyVRRenderSize $saved $defaultWidth $defaultHeight $false $false) 2016 2160 $false
Save-PreyVRConfiguration $config $saved @{Width=2016;Height=2160} $false
if (@(Get-ChildItem -LiteralPath $testDir -Filter '*.bak').Count -ne 1) {throw 'Repeated startup repeated migration'}
Write-Output 'PASS: defaults, migration, custom/explicit/versioned sizes, partial/invalid refusal, exact backup, settings preservation and idempotence.'
