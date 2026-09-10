[CmdletBinding()]
param(
    [string]$GameExe='',
    [string]$PackageDir='',
    [string]$Runtime='',
    [string]$RunRoot="$env:LOCALAPPDATA\PreyVR\runs",
    [int]$Width=2560,
    [int]$Height=1440,
    [int]$HudLayer=1,
    [int]$UiCurveDegrees=35,
    [int]$PointerHand=1,
    # **The HUD/menu panel size, 20..200 percent.** Runtime-only until now: it
    # could be sent by hand while playing but was gone the next launch, so a
    # wearer had to rediscover their number every session. Persisted with the
    # rest below and re-sent once VR is up.
    [int]$UiScalePercent=100,
    # The onboarding card under the menu. Off by default since 2026-09-11: it is
    # read once and then costs about a sixth of the panel on every menu after.
    # Set to 1 when handing the package to someone who has not seen the controls.
    [int]$UiGuide=0,
    [switch]$DryRun
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
if (-not $PackageDir) { $PackageDir = $PSScriptRoot }
if (-not $PackageDir) { $PackageDir = Split-Path -Parent $PSCommandPath }
try {
    $config=Join-Path $PackageDir 'PreyVR.json'
    if (Test-Path -LiteralPath $config) {
        $saved=Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
        if (!$GameExe -and $saved.PSObject.Properties['GameExe']) {$GameExe=$saved.GameExe}
        if (!$PSBoundParameters.ContainsKey('Width') -and $saved.PSObject.Properties['Width']) {$Width=[int]$saved.Width}
        if (!$PSBoundParameters.ContainsKey('Height') -and $saved.PSObject.Properties['Height']) {$Height=[int]$saved.Height}
        if (!$PSBoundParameters.ContainsKey('HudLayer') -and $saved.PSObject.Properties['HudLayer']) {$HudLayer=[int]$saved.HudLayer}
        if (!$PSBoundParameters.ContainsKey('UiCurveDegrees') -and $saved.PSObject.Properties['UiCurveDegrees']) {$UiCurveDegrees=[int]$saved.UiCurveDegrees}
        if (!$PSBoundParameters.ContainsKey('PointerHand') -and $saved.PSObject.Properties['PointerHand']) {$PointerHand=[int]$saved.PointerHand}
        if (!$PSBoundParameters.ContainsKey('UiScalePercent') -and $saved.PSObject.Properties['UiScalePercent']) {$UiScalePercent=[int]$saved.UiScalePercent}
        if (!$PSBoundParameters.ContainsKey('UiGuide') -and $saved.PSObject.Properties['UiGuide']) {$UiGuide=[int]$saved.UiGuide}
    }
    if ($Width -lt 1280 -or $Width -gt 8192 -or $Height -lt 720 -or $Height -gt 8192) {
        throw 'Render size must be 1280..8192 by 720..8192.'
    }
    if ($HudLayer -notin @(0,1)) {throw 'HudLayer must be 0 or 1.'}
    if ($UiCurveDegrees -lt 0 -or $UiCurveDegrees -gt 60) {throw 'UiCurveDegrees must be 0..60 (0 is flat).'}
    if ($PointerHand -notin @(0,1,2)) {throw 'PointerHand must be 0 (left), 1 (right), or 2 (buttons only).'}
    if ($UiScalePercent -lt 20 -or $UiScalePercent -gt 200) {throw 'UiScalePercent must be 20..200.'}
    if ($UiGuide -notin @(0,1)) {throw 'UiGuide must be 0 (hidden) or 1 (shown).'}
    if (!$GameExe) {
        $candidates=@()
        foreach ($key in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 480490',
                           'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 480490')) {
            $install=Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
            if ($install -and $install.InstallLocation) {
                $candidates+=Join-Path $install.InstallLocation 'Binaries\Danielle\x64\Release\Prey.exe'
            }
        }
        $GameExe=$candidates | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
    }
    if (!$GameExe -or !(Test-Path -LiteralPath $GameExe)) {
        if ($DryRun) {throw 'Supply -GameExe for this preflight.'}
        Add-Type -AssemblyName System.Windows.Forms
        $picker=New-Object System.Windows.Forms.OpenFileDialog
        $picker.Title='Choose Prey.exe (Prey 2017, Steam version)'
        $picker.Filter='Prey executable|Prey.exe'
        if ($picker.ShowDialog() -ne 'OK') {exit 0}
        $GameExe=$picker.FileName
    }
    $GameExe=(Resolve-Path -LiteralPath $GameExe).Path
    $release=Split-Path -Parent $GameExe
    $gameDll=Join-Path $release 'PreyDll.dll'
    $supported='7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7'
    if ((Get-FileHash -LiteralPath $gameDll -Algorithm SHA256).Hash -ne $supported) {
        throw 'This Prey build is not supported by this package. No mod was loaded.'
    }
    # **The binaries are not always beside this script.** In the player package
    # they are. In the repo, tools/ holds the launcher and nothing else, so a
    # wearer who opened tools\ in Explorer and double-clicked "Start Prey VR.cmd"
    # -- an identical-looking file to the packaged one -- got
    # "Package file missing: PreyVR.dll" and no way to act on it.
    #
    # Fall back to the build output, and print which directory won either way,
    # because the failure mode this replaces was two launchers that look the
    # same and behave differently.
    $needed=@('PreyVR.dll','openxr_loader.dll','preyvr_injector.exe')
    $BinDir=$null
    foreach ($candidate in @($PackageDir,(Join-Path $PackageDir '../build/headless/Release'))) {
        $present=@($needed | Where-Object {Test-Path -LiteralPath (Join-Path $candidate $_)})
        if ($present.Count -eq $needed.Count) {$BinDir=(Resolve-Path -LiteralPath $candidate).Path;break}
    }
    if (!$BinDir) {
        throw ("Mod binaries not found beside {0} or in its build output. Missing: {1}. Run the launcher from the player package, or build first." -f
            $PackageDir,(($needed | Where-Object {!(Test-Path -LiteralPath (Join-Path $PackageDir $_))}) -join ', '))
    }
    if ($Runtime -and !(Test-Path -LiteralPath $Runtime)) {throw 'OpenXR runtime manifest does not exist.'}
    # Never boot another fleet game over an active test session.
    $fleetNames=@('Prey','Dishonored','Bioshock','BioshockHD','FarCry2','Soma','Shock2',
        'SystemShock2','SystemShock2Remastered','Swat4','Swat4X','SoF','TS4_x64','moh','moh_s','Il-2','Launcher64','carrier_command_2')
    $running=@(Get-Process -Name $fleetNames -ErrorAction SilentlyContinue)
    if ($running.Count) {throw ('Close the running game first: '+(($running | Select-Object -ExpandProperty ProcessName) -join ', '))}
    Write-Host "Prey VR: $Width x $Height per eye; UI scale $UiScalePercent%; guide $(if($UiGuide){'on'}else{'off'}); runtime: $(if($Runtime){$Runtime}else{'system OpenXR runtime'})"
    Write-Host "Mod binaries: $BinDir"
    Write-Host 'Controls: menu button = pause; right stick = navigate; A = select; B = back.'
    Write-Host 'Point + beam-hand trigger = click/drag; grips = tabs; X/Y = actions.'
    Write-Host 'F12 or both grips = reset view. Press A at the title/loading prompt.'
    Write-Host 'Tilde (the key below Esc) opens Prey''s own console, when focused.'
    if ($DryRun) {Write-Host 'Preflight passed. Nothing launched.';exit 0}
    @{GameExe=$GameExe;Width=$Width;Height=$Height;HudLayer=$HudLayer;UiCurveDegrees=$UiCurveDegrees;PointerHand=$PointerHand;UiScalePercent=$UiScalePercent;UiGuide=$UiGuide} | ConvertTo-Json | Set-Content -LiteralPath $config -Encoding UTF8
    $run=Join-Path $RunRoot ('player-{0:yyyyMMdd-HHmmss}' -f (Get-Date))
    [void](New-Item -ItemType Directory -Path $run -Force)
    $info=New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName=$GameExe
    $info.WorkingDirectory=(Get-Item -LiteralPath $release).Parent.Parent.Parent.Parent.FullName
    $info.UseShellExecute=$false
    $info.Arguments="+r_Fullscreen 0 +r_Width $Width +r_Height $Height"
    $info.EnvironmentVariables['SteamAppId']='480490'
    $info.EnvironmentVariables['PREYVR_LOG_PATH']=Join-Path $run 'PreyVR.log'
    $info.EnvironmentVariables['PREYVR_RENDER_WIDTH']="$Width"
    $info.EnvironmentVariables['PREYVR_RENDER_HEIGHT']="$Height"
    $info.EnvironmentVariables['PREYVR_HUD_LAYER']="$HudLayer"
    $info.EnvironmentVariables['PREYVR_UI_CURVE_DEGREES']="$UiCurveDegrees"
    $info.EnvironmentVariables['PREYVR_POINTER_HAND']="$PointerHand"
    # Runtime overrides and simulator state remain process-scoped. The normal
    # double-click path uses the player's selected runtime (VDXR/SteamVR/etc.).
    if ($Runtime) {$info.EnvironmentVariables['XR_RUNTIME_JSON']=(Resolve-Path -LiteralPath $Runtime).Path}
    $process=[System.Diagnostics.Process]::Start($info)
    Write-Host "Starting Prey (PID $($process.Id))..."
    $deadline=(Get-Date).AddSeconds(120)
    $ready=$false
    while ((Get-Date) -lt $deadline) {
        if ($process.HasExited) {throw 'Prey exited or Steam replaced the process. Start Steam and retry.'}
        try {
            $process.Refresh()
            $ready=($process.MainWindowHandle -ne 0) -and
                (@($process.Modules | Where-Object {$_.ModuleName -eq 'PreyDll.dll'}).Count -eq 1)
        } catch {$ready=$false}
        if ($ready) {break}
        Start-Sleep -Milliseconds 250
    }
    if (!$ready) {throw 'Timed out waiting for Prey to initialize. The game is left running.'}
    # Allow the window/device initialization to finish before the signature gate.
    Start-Sleep -Seconds 3
    & (Join-Path $BinDir 'preyvr_injector.exe') $process.Id (Join-Path $BinDir 'PreyVR.dll')
    if ($LASTEXITCODE -ne 0) {throw "Mod load failed (injector code $LASTEXITCODE)."}
    $log=Join-Path $run 'PreyVR.log'
    $deadline=(Get-Date).AddSeconds(20)
    do {
        if ((Test-Path -LiteralPath $log) -and
            (Select-String -LiteralPath $log -SimpleMatch 'preyvr_channel result=0 detail=started' -Quiet)) {break}
        if ((Get-Date) -gt $deadline) {throw "Mod startup did not finish. See $log"}
        Start-Sleep -Milliseconds 200
    } while ($true)
    'vr.enable' | Set-Content -LiteralPath (Join-Path $run 'commands.txt') -Encoding ASCII
    $deadline=(Get-Date).AddSeconds(150)
    Write-Host 'Put on the headset. Enabling VR...'
    do {
        $text=Get-Content -LiteralPath $log -Raw
        if ($text -match 'preyvr_vr state=failed step=([^\s]+)') {throw "VR setup failed at $($Matches[1]). See $log (F11 retries)."}
        if ($text -match 'preyvr_vr state=active') {break}
        if ($process.HasExited) {throw 'Prey closed during VR setup.'}
        if ((Get-Date) -gt $deadline) {throw "Waiting for VR timed out. See $log"}
        Start-Sleep -Milliseconds 250
    } while ($true)
    # Only after `state=active`: the panels are rebuilt from the fit, and a scale
    # sent before the session exists has nothing to rebuild.
    if ($UiScalePercent -ne 100) {
        "ui.scale $UiScalePercent" | Set-Content -LiteralPath (Join-Path $run 'commands.txt') -Encoding ASCII
        Start-Sleep -Milliseconds 400
    }
    if ($UiGuide -ne 0) {
        "ui.guide 1" | Set-Content -LiteralPath (Join-Path $run 'commands.txt') -Encoding ASCII
        Start-Sleep -Milliseconds 400
    }
    # Starts the keyboard bridge, which nothing else does. Without it the ~
    # console key and the Ctrl+Alt adjustments are all inert.
    "input.hotkeys 1" | Set-Content -LiteralPath (Join-Path $run 'commands.txt') -Encoding ASCII
    Write-Host "VR is ready. Diagnostics: $run"
    Write-Host ''
    Write-Host 'To adjust while playing, run "Tune Prey VR.cmd" beside this launcher'
    Write-Host 'and type e.g.  ui.scale 130   (20..200; the value is remembered).'
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    if (!$DryRun) {Read-Host 'Press Enter to close' | Out-Null}
    exit 1
}
