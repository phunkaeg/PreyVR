[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$scriptPath=Join-Path $PSScriptRoot '../tools/Start-PreyVR.ps1'
$tokens=$null;$parseErrors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile($scriptPath,[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count) {throw ($parseErrors | Out-String)}
# Extract configuration only. Never dot-source or invoke the launcher body.
foreach ($name in @('Resolve-PreyVRComfortSettings','Get-PreyVRStartupCommands','Save-PreyVRConfiguration')) {
    $function=$ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true)
    if (!$function) {throw "Missing $name"}
    Invoke-Expression $function.Extent.Text
}
function Assert-Comfort($result,$snap,$relative,$space) {
    if ($result.SnapTurnDegrees -ne $snap -or $result.HeadRelativeMovement -ne $relative -or $result.ReferenceSpace -cne $space) {
        throw "Unexpected comfort selection: $($result | ConvertTo-Json -Compress)"
    }
}
function Default-Value($name) {
    $ast.ParamBlock.Parameters.Where({$_.Name.VariablePath.UserPath -eq $name})[0].DefaultValue.Value
}
$defaults=Resolve-PreyVRComfortSettings $null @{} (Default-Value 'SnapTurnDegrees') (Default-Value 'HeadRelativeMovement') (Default-Value 'ReferenceSpace')
Assert-Comfort $defaults 45 1 'auto'
$saved=[pscustomobject]@{SnapTurnDegrees=0;HeadRelativeMovement=0;ReferenceSpace='local';UiScalePercent=137;ExtensionSetting='keep'}
Assert-Comfort (Resolve-PreyVRComfortSettings $saved @{} 45 1 'auto') 0 0 'local'
Assert-Comfort (Resolve-PreyVRComfortSettings $saved @{SnapTurnDegrees=1;HeadRelativeMovement=1;ReferenceSpace=1} 30 1 'AUTO') 30 1 'auto'
Assert-Comfort (Resolve-PreyVRComfortSettings $saved @{SnapTurnDegrees=1} 60 1 'auto') 60 0 'local'
Assert-Comfort (Resolve-PreyVRComfortSettings ([pscustomobject]@{UiScalePercent=137}) @{} 45 1 'auto') 45 1 'auto'
foreach ($snap in @(0,15,90)) {Assert-Comfort (Resolve-PreyVRComfortSettings $null @{} $snap 1 'auto') $snap 1 'auto'}
foreach ($values in @(@(-1,1,'auto'),@(14,1,'auto'),@(91,1,'auto'),@(45,2,'auto'),@(45,-1,'auto'),@(45,1,'stage'),@(45,1,''))) {
    $rejected=$false
    try {$null=Resolve-PreyVRComfortSettings $null @{} @values} catch {$rejected=$true}
    if (!$rejected) {throw "Invalid comfort settings accepted: $values"}
}
$rejected=$false
try {$null=Resolve-PreyVRComfortSettings ([pscustomobject]@{SnapTurnDegrees=999}) @{} 45 1 'auto'} catch {$rejected=$true}
if (!$rejected) {throw 'Invalid saved setting bypassed validation'}

$testDir=Join-Path $PSScriptRoot ('../build/launcher-comfort-tests/'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $testDir -Force)
$config=Join-Path $testDir 'PreyVR.json'
Save-PreyVRConfiguration $config $saved @{SnapTurnDegrees=30;HeadRelativeMovement=1;ReferenceSpace='auto'} $false
$reloaded=Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
Assert-Comfort (Resolve-PreyVRComfortSettings $reloaded @{} 45 0 'local') 30 1 'auto'
if ($reloaded.UiScalePercent -ne 137 -or $reloaded.ExtensionSetting -ne 'keep') {throw 'Comfort save lost unrelated settings'}

$commands=@(Get-PreyVRStartupCommands (Resolve-PreyVRComfortSettings $reloaded @{} 45 0 'local') 137 1)
$expected=@('move.snap 30','move.headrelative 1','ui.scale 137','ui.guide 1','input.hotkeys 1','vr.enable')
if (($commands -join "`n") -cne ($expected -join "`n")) {throw 'Startup must apply saved settings before enabling VR'}
$smooth=Resolve-PreyVRComfortSettings $null @{} 0 0 'local'
$commands=@(Get-PreyVRStartupCommands $smooth 100 0)
if ($commands[0] -ne 'move.snap 0' -or $commands[1] -ne 'move.headrelative 0' -or $commands[3] -ne 'ui.guide 0') {throw 'Explicit off values were lost'}
$commands=@(Get-PreyVRStartupCommands $smooth 100 0 255)
if ($commands[-2] -ne 'dbg.draw 255' -or $commands[-1] -ne 'vr.enable') {throw 'Debug overlay must be requested before enabling VR'}
$commands=@(Get-PreyVRStartupCommands $smooth 100 0 0 $true)
if ($commands[-2] -ne 'aim.shot 1' -or $commands[-1] -ne 'vr.enable') {throw 'Muzzle aim must be requested before enabling VR'}
if (@(Get-PreyVRStartupCommands $smooth 100 0) -contains 'aim.shot 1') {throw 'Muzzle aim must stay opt-in'}
Write-Output 'PASS: comfort defaults, saved/explicit precedence, range refusal, restart persistence, settings preservation and ordered startup commands.'
