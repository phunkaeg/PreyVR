# Execute the real bridge's bounded file/trajectory code against a temporary
# fake state reader. No process lookup, game, OpenXR runtime or shared files.
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
$path=Join-Path $root 'tools/regression/bridge.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Bridge parse failed'}
$functions=$ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -in @('Fast-Sim','Send-Mod')},$true)
if($functions.Count -ne 2){throw 'Expected production command protocol functions'}
foreach($function in $functions){. ([scriptblock]::Create($function.Extent.Text))}
$switch=$ast.Find({param($n)$n -is [Management.Automation.Language.SwitchStatementAst]},$true)
$body=@($switch.Clauses | Where-Object {$_.Item1.Value -eq 'motion'})[0].Item2.Extent.Text
$motion=[scriptblock]::Create($body.Substring(1,$body.Length-2))
$temp=Join-Path ([IO.Path]::GetTempPath()) ('preyvr-bridge-offline-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
$originalCulture=[cultureinfo]::CurrentCulture
try{
    $q=[pscustomobject]@{stateDir=$temp;runDir=$temp;timeout=.1;ownerPid=123;start=@(-.2,1.35,-.35);delta=@(0,0,-.08);durationMs=100;release=$true}
    $script:state=[pscustomobject]@{pid=123;cmdSeq=0;frame=1;sessionRunning=$true;sessionState='FOCUSED';lastCmd='';lastCmdError=''}
    $script:lastText=''
    function Read-State {
        $file=Join-Path $q.stateDir 'command.txt'
        if(Test-Path -LiteralPath $file){
            $current=[IO.File]::ReadAllText($file)
            if($current -ne $script:lastText){
                $rows=@($current -split "`n" | Where-Object {$_})
                $script:state.cmdSeq+=$rows.Count;$script:state.lastCmd=$rows[-1];$script:lastText=$current
            }
        }
        $script:state.frame++
        return $script:state
    }
    [cultureinfo]::CurrentCulture=[cultureinfo]'de-DE'
    . $motion
    $written=[IO.File]::ReadAllText((Join-Path $temp 'command.txt'))
    if($result.steps -lt 2 -or $written -notmatch 'hand l grip pos -0\.200000 1\.350000 -0\.430000' -or $written -notmatch 'grip l 0'){
        throw 'Production physical stroke did not end at expected raw XR point with atomic release'
    }
    $q.durationMs=99;$refused=$false
    try{. $motion}catch{$refused=$true}
    if(!$refused -or [IO.File]::ReadAllText((Join-Path $temp 'command.txt')) -ne $written){throw 'Invalid duration was not refused before IO'}
    $script:state.lastCmdError='fixture rejection';$refused=$false
    try{Fast-Sim @('head valid off')}catch{$refused=$true}
    if(!$refused){throw 'A rejected acknowledgement passed'}
    # Actual native-channel writer/correlation reader, with a local response
    # function in place of the game poller. First query must create the file.
    $nativeCmd=Join-Path $temp 'commands.txt';$nativeRes=Join-Path $temp 'results.txt'
    [IO.File]::WriteAllText($nativeRes,'')
    $script:wrongNonce=$false
    function Get-Content {
        param([string]$LiteralPath,[switch]$Raw)
        if($LiteralPath -ne $nativeRes){throw 'Unexpected fixture read'}
        $written=[IO.File]::ReadAllText($nativeCmd)
        $nonce=($written.Trim() -split "`n")[-1].Split(' ')[1]
        if($script:wrongNonce){$nonce='old-response'}
        return (@{schema=1;result=0;request=$nonce;pid=123}|ConvertTo-Json -Compress)
    }
    $reply=Send-Mod ''
    if($reply.probe.request -ne $reply.request -or !(Test-Path -LiteralPath $nativeCmd)){throw 'First native query creation/correlation failed'}
    [IO.File]::WriteAllText($nativeCmd,'');$script:wrongNonce=$true;$refused=$false
    try{Send-Mod ''|Out-Null}catch{$refused=$true}
    if(!$refused){throw 'Stale native response was accepted'}
    [IO.File]::WriteAllText($nativeCmd,'another owner');$refused=$false
    try{Send-Mod ''|Out-Null}catch{$refused=$true}
    if(!$refused -or [IO.File]::ReadAllText($nativeCmd) -ne 'another owner'){throw 'Pending native command was overwritten'}
    Write-Output 'Bridge file protocol and bounded physical stroke passed against offline fake state; no game/runtime calls.'
}finally{
    [cultureinfo]::CurrentCulture=$originalCulture
    # Only these explicitly created files; no recursive deletion.
    foreach($name in @('command.txt','commands.txt','results.txt')){
        $file=Join-Path $temp $name;if(Test-Path -LiteralPath $file){Remove-Item -LiteralPath $file}
    }
    Remove-Item -LiteralPath $temp
}
