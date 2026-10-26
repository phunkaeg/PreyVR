<#
.SYNOPSIS
Plan or explicitly execute an attach-only xr-sim playtest. Never starts Prey.
.DESCRIPTION
Default is plan only. -Execute requires explicit native channel, xr-sim state,
tested DLL and a prepared game fixture. See docs/REGRESSION-RUNNER.md.
#>
[CmdletBinding()]
param(
 [switch]$Execute,
 [string]$RunDir='', [string]$StateDir='', [string]$Dll='',
 [string]$Fixture=(Join-Path $PSScriptRoot 'regression/default-fixture.json'),
 [string]$Out='', [string]$Baseline='',
 [string]$Scenarios='menu,inventory,two_hand,zero_g',
 [string]$Label='candidate',
 [string]$XrSimRoot='D:/Dev Debug/xr-sim',
 [string]$Python='',
 [string]$PowerShell=(Get-Process -Id $PID).Path
)
$ErrorActionPreference='Stop'
if(!$Python){
 $candidates=@((Join-Path $env:LOCALAPPDATA 'Programs/Python/Python312/python.exe'))
 $candidates+=@(Get-Command python3,python -CommandType Application -ErrorAction SilentlyContinue | ForEach-Object Source)
 foreach($candidate in $candidates){
  if(!(Test-Path -LiteralPath $candidate -PathType Leaf) -or $candidate -match '\\(?:WindowsApps|\.pyenv)\\'){continue}
  & $candidate -E -B -c 'import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)'
  if($LASTEXITCODE -eq 0){$Python=$candidate;break}
 }
 if(!$Python){throw 'Python 3.10+ not found. Pass -Python with a verified interpreter path.'}
}
$arguments=@('-E','-B',(Join-Path $PSScriptRoot 'regression/runner.py'),'--fixture',$Fixture,'--scenarios',$Scenarios,'--label',$Label)
if($Execute){
 foreach($entry in @(@('RunDir',$RunDir),@('StateDir',$StateDir),@('Dll',$Dll),@('Out',$Out))){if(!$entry[1]){throw "-Execute requires -$($entry[0])"}}
 $arguments+=@('--execute','--run-dir',$RunDir,'--state-dir',$StateDir,'--dll',$Dll,'--out',$Out,'--sim-root',$XrSimRoot,'--powershell',$PowerShell)
}
if($Baseline){$arguments+=@('--baseline',$Baseline)}
& $Python @arguments
exit $LASTEXITCODE
