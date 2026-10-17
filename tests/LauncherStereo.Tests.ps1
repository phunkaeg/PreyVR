[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$scriptPath=Join-Path $PSScriptRoot '../tools/Start-PreyVR.ps1'
$tokens=$null;$parseErrors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile($scriptPath,[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count) {throw ($parseErrors | Out-String)}
# Extract configuration only. Never dot-source or invoke the launcher body.
foreach ($name in @('Resolve-PreyVRStereoSettings','Get-PreyVRStereoStatus','Save-PreyVRConfiguration')) {
    $function=$ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true)
    if (!$function) {throw "Missing $name"}
    Invoke-Expression $function.Extent.Text
}
function Assert-Stereo($result,$expected) {
    if ($result -ne $expected) {throw "Unexpected DualStereo selection: $result (expected $expected)"}
}
$default=[int]$ast.ParamBlock.Parameters.Where({$_.Name.VariablePath.UserPath -eq 'DualStereo'})[0].DefaultValue.Value
Assert-Stereo (Resolve-PreyVRStereoSettings $null @{} $default) 1
$saved=[pscustomobject]@{DualStereo=0;UiScalePercent=137;ExtensionSetting='keep'}
Assert-Stereo (Resolve-PreyVRStereoSettings $saved @{} $default) 0
Assert-Stereo (Resolve-PreyVRStereoSettings $saved @{DualStereo=1} 1) 1
Assert-Stereo (Resolve-PreyVRStereoSettings ([pscustomobject]@{UiScalePercent=137}) @{} $default) 1
foreach ($value in @(-1,2)) {
    $rejected=$false
    try {$null=Resolve-PreyVRStereoSettings $null @{} $value} catch {$rejected=$true}
    if (!$rejected) {throw "Invalid DualStereo accepted: $value"}
}
$rejected=$false
try {$null=Resolve-PreyVRStereoSettings ([pscustomobject]@{DualStereo=5}) @{} 1} catch {$rejected=$true}
if (!$rejected) {throw 'Invalid saved DualStereo bypassed validation'}

$testDir=Join-Path $PSScriptRoot ('../build/launcher-stereo-tests/'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $testDir -Force)
$config=Join-Path $testDir 'PreyVR.json'
Save-PreyVRConfiguration $config $saved @{DualStereo=0} $false
$reloaded=Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
Assert-Stereo (Resolve-PreyVRStereoSettings $reloaded @{} 1) 0
if ($reloaded.UiScalePercent -ne 137 -or $reloaded.ExtensionSetting -ne 'keep') {throw 'Stereo save lost unrelated settings'}

$log="preyvr_vr state=starting renderer=settled`npreyvr_vr dual_stereo=on`npreyvr_vr state=active panel=automatic"
if ((Get-PreyVRStereoStatus $log) -ne 'on') {throw 'Both-eyes start not recognised'}
if ((Get-PreyVRStereoStatus 'preyvr_vr dual_stereo=off reason=PREYVR_DUAL_STEREO') -ne 'off') {throw 'Requested alternate eyes not recognised'}
if ((Get-PreyVRStereoStatus 'preyvr_vr dual_stereo=unavailable fallback=alternate_eye') -ne 'unavailable') {throw 'Fallback not recognised'}
if ((Get-PreyVRStereoStatus "preyvr_vr dual_stereo=unavailable fallback=alternate_eye`npreyvr_vr dual_stereo=on") -ne 'on') {throw 'A retried start must report its latest mode'}
if ((Get-PreyVRStereoStatus 'preyvr_vr state=active') -ne 'unknown') {throw 'A log without the mode must not claim one'}

# The launcher and the DLL must agree on the variable and the log lines.
$launcher=Get-Content -LiteralPath $scriptPath -Raw
$vrMode=Get-Content -LiteralPath (Join-Path $PSScriptRoot '../src/dll/VrMode.cpp') -Raw
if ($launcher -notmatch [regex]::Escape("EnvironmentVariables['PREYVR_DUAL_STEREO']")) {throw 'Launcher no longer passes PREYVR_DUAL_STEREO'}
if ($vrMode -notmatch [regex]::Escape('L"PREYVR_DUAL_STEREO"')) {throw 'VrMode no longer reads PREYVR_DUAL_STEREO'}
foreach ($line in @('dual_stereo=on','dual_stereo=off','dual_stereo=unavailable')) {
    if ($vrMode -notmatch [regex]::Escape("Log(`"$line")) {throw "VrMode no longer logs $line"}
}
Write-Output 'PASS: stereo default, saved/explicit precedence, range refusal, restart persistence, settings preservation, status parsing and launcher/DLL contract.'
