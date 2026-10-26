# Windows adapter for runner.py. Never launches, attaches a debugger or injects.
# The existing xr-sim command/capture helpers are used except for the bounded
# hand trajectory, whose per-frame acknowledgements need finer polling than
# xrsim-cmd's 100 ms interval. That path uses the documented same file protocol.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RequestFile)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$q=Get-Content -LiteralPath $RequestFile -Raw | ConvertFrom-Json
function Read-State {
    for($attempt=0;$attempt -lt 5;$attempt++){
        try{return Get-Content -LiteralPath (Join-Path $q.stateDir 'state.json') -Raw | ConvertFrom-Json}catch{Start-Sleep -Milliseconds 10}
    }
    throw 'No readable xr-sim state'
}
function Assert-Owner {
    $s=Read-State
    if($s.runtime -ne 'xr-sim' -or $s.graphics -ne 'D3D11' -or !$s.sessionRunning -or $s.sessionState -ne 'FOCUSED' -or $s.paceMode -ne 'free'){
        throw 'Requires a live FOCUSED D3D11 xr-sim session with free pacing'
    }
    $process=Get-Process -Id ([int]$s.pid) -ErrorAction Stop
    if($process.ProcessName -ne 'Prey'){throw 'xr-sim PID is not Prey'}
    if($q.ownerPid -and ([int]$s.pid -ne [int]$q.ownerPid -or "$($process.StartTime.ToUniversalTime().Ticks)" -ne "$($q.ownerStart)")){
        throw 'Prey PID/start identity changed; refusing to drive replacement process'
    }
    return @{state=$s;process=$process}
}
function Send-Mod([string]$line){
    if($line.Contains("`n") -or $line.Contains("`r")){throw 'One native command per request'}
    $nonce=[guid]::NewGuid().ToString('N')
    $cmd=Join-Path $q.runDir 'commands.txt';$res=Join-Path $q.runDir 'results.txt'
    if((Test-Path -LiteralPath $cmd) -and (Get-Item -LiteralPath $cmd).Length -gt 0){throw 'Native channel busy; refusing to overwrite pending commands'}
    $batch=if($line){"$line`nregression.state $nonce`n"}else{"regression.state $nonce`n"}
    $tmp=Join-Path $q.runDir ("regression-$nonce.tmp")
    try{
        [IO.File]::WriteAllText($tmp,$batch,[Text.UTF8Encoding]::new($false))
        if(Test-Path -LiteralPath $cmd){[IO.File]::Replace($tmp,$cmd,$null)}else{[IO.File]::Move($tmp,$cmd)}
    }finally{if(Test-Path -LiteralPath $tmp){Remove-Item -LiteralPath $tmp}}
    $timer=[Diagnostics.Stopwatch]::StartNew()
    while($timer.Elapsed.TotalSeconds -lt $q.timeout){
        if(Test-Path -LiteralPath $res){
            try{
                $text=Get-Content -LiteralPath $res -Raw
                foreach($row in ($text -split "`n")){
                    if(!$row.StartsWith('{')){continue}
                    try{$probe=$row | ConvertFrom-Json}catch{continue}
                    if($probe.PSObject.Properties['request'] -and $probe.request -eq $nonce){
                        if($probe.result -ne 0 -or $probe.pid -ne $q.ownerPid){throw 'Native diagnostic reply belongs to another process or failed'}
                        return @{probe=$probe;reply=$text;request=$nonce}
                    }
                }
            }catch [IO.IOException]{}
        }
        Start-Sleep -Milliseconds 20
    }
    throw 'Native response timed out (matching regression.state nonce required)'
}
function Fast-Sim([string[]]$lines){
    $s=Read-State;$expected=[long]$s.cmdSeq+$lines.Count
    [IO.File]::WriteAllText((Join-Path $q.stateDir 'command.txt'),(($lines -join "`n")+"`n"),[Text.UTF8Encoding]::new($false))
    $timer=[Diagnostics.Stopwatch]::StartNew()
    while($timer.ElapsedMilliseconds -lt 1500){
        $s=Read-State
        if($s.pid -ne $q.ownerPid -or !$s.sessionRunning -or $s.sessionState -ne 'FOCUSED'){throw 'Session lost during physical trajectory'}
        if([long]$s.cmdSeq -ge $expected){
            if($s.lastCmdError -or $s.lastCmd -ne $lines[-1]){throw 'Trajectory command rejected or another writer owns the channel'}
            return
        }
        Start-Sleep -Milliseconds 1
    }
    throw 'Trajectory acknowledgement timed out'
}
try{
    if($q.schema -ne 1){throw 'Unknown bridge request schema'}
    $owner=Assert-Owner;$state=$owner.state
    switch($q.action){
        'preflight' {
            $native=@($owner.process.Modules | Where-Object ModuleName -eq 'PreyDll.dll')
            if($native.Count -ne 1){throw 'Could not identify the loaded native Prey module'}
            $hash=(Get-FileHash -LiteralPath $native[0].FileName -Algorithm SHA256).Hash.ToLowerInvariant()
            if($hash -ne '7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7'){throw 'Unsupported PreyDll.dll'}
            $startup=Get-Content -LiteralPath (Join-Path $q.runDir 'PreyVR.log') -TotalCount 35
            $identity=@($startup | Where-Object {$_ -match 'preyvr_smoke_start.*dllSha256=([a-fA-F0-9]{64})'})
            if($identity.Count -ne 1 -or $identity[0] -notmatch 'dllSha256=([a-fA-F0-9]{64})'){throw 'Missing unique loaded DLL identity in mod startup log'}
            $result=@{state=$state;ownerPid=$state.pid;ownerStart="$($owner.process.StartTime.ToUniversalTime().Ticks)";
                gameModule=$native[0].FileName;gameHash=$hash;loadedDllHash=$Matches[1].ToLowerInvariant();exe=$owner.process.Path}
        }
        'status' {$result=@{state=$state}}
        'sample' {$result=Send-Mod ''}
        'mod' {$result=Send-Mod $q.command}
        'sim' {
            & (Join-Path $q.simRoot 'tools/xrsim-cmd.ps1') -Dir $q.stateDir -TimeoutSec $q.timeout -Quiet -Lines $q.lines | Out-Null
            $result=@{state=(Read-State)}
        }
        'capture' {
            $shot=& (Join-Path $q.simRoot 'tools/xrsim-shot.ps1') -Dir $q.stateDir -Out $q.outBase -Quiet -TimeoutSec $q.timeout
            $result=@{state=(Read-State);shot=$shot}
        }
        'pulse' {
            if($q.durationMs -lt 50 -or $q.durationMs -gt 250){throw 'Pulse must last 50..250 ms'}
            try{Fast-Sim $q.lines;Start-Sleep -Milliseconds $q.durationMs}
            finally{Fast-Sim $q.releaseLines}
            $result=@{state=(Read-State)}
        }
        'motion' {
            if($q.durationMs -lt 100 -or $q.durationMs -gt 750){throw 'Physical stroke must take 100..750 ms'}
            $start=@($q.start);$delta=@($q.delta)
            if($start.Count -ne 3 -or $delta.Count -ne 3){throw 'Stroke needs two 3-vectors'}
            $distance=0.0;for($axis=0;$axis -lt 3;$axis++){
                if([double]::IsNaN($start[$axis]) -or [double]::IsInfinity($start[$axis]) -or [math]::Abs($start[$axis]) -gt 4 -or
                   [double]::IsNaN($delta[$axis]) -or [double]::IsInfinity($delta[$axis])){throw 'Invalid physical stroke coordinate'}
                $distance+=[double]$delta[$axis]*[double]$delta[$axis]
            }
            if($distance -lt .0001 -or $distance -gt .0324){throw 'Stroke distance must be 1..18 cm'}
            $clock=[Diagnostics.Stopwatch]::StartNew();$steps=0
            do{
                $t=[math]::Min(1.0,$clock.Elapsed.TotalMilliseconds/$q.durationMs)
                $pos=for($axis=0;$axis -lt 3;$axis++){[double]$start[$axis]+[double]$delta[$axis]*$t}
                $argsText=[string]::Format([cultureinfo]::InvariantCulture,'{0:F6} {1:F6} {2:F6}',$pos[0],$pos[1],$pos[2])
                $lines=@("hand l grip pos $argsText","hand l aim pos $argsText")
                if($t -ge 1 -and $q.release){$lines+='grip l 0'}
                Fast-Sim $lines;$steps++
                if($t -lt 1){Start-Sleep -Milliseconds 8}
            }while($t -lt 1)
            $result=@{state=(Read-State);steps=$steps;elapsedMs=$clock.Elapsed.TotalMilliseconds}
        }
        default {throw 'Unknown bridge action'}
    }
    @{ok=$true;data=$result} | ConvertTo-Json -Depth 32 -Compress
}catch{
    @{ok=$false;error=$_.Exception.Message} | ConvertTo-Json -Compress
    exit 1
}
