[CmdletBinding()]
param(
    [string]$GameExe='',
    [string]$PackageDir='',
    [string]$Runtime='',
    [string]$RunRoot="$env:LOCALAPPDATA\PreyVR\runs",
    # **Per eye, about the headset's own density** (Meta asks ~1232 x 1360 at
    # Link's default; Prey's wider field needs a little more). 2016 x 2160 was
    # 2.5x the pixels the runtime asked for and held ~19 fps on the RTX 3060
    # laptop (headset, 2026-10-17). Same 0.933 aspect, so the view is unchanged.
    [int]$Width=1344,
    [int]$Height=1440,
    [int]$HudLayer=1,
    [int]$UiCurveDegrees=35,
    [int]$PointerHand=1,
    # **The HUD/menu panel size, 20..200 percent.** Runtime-only until now: it
    # could be sent by hand while playing but was gone the next launch, so a
    # wearer had to rediscover their number every session. Persisted with the
    # rest below and sent before VR is enabled.
    [int]$UiScalePercent=100,
    # The onboarding card under the menu. Off by default since 2026-09-11: it is
    # read once and then costs about a sixth of the panel on every menu after.
    # Set to 1 when handing the package to someone who has not seen the controls.
    [int]$UiGuide=0,
    [int]$SnapTurnDegrees=45,
    [int]$HeadRelativeMovement=1,
    # auto prefers LOCAL_FLOOR, then STAGE, with LOCAL fallback.
    [string]$ReferenceSpace='auto',
    # **Both eyes from one game frame (1) or alternate eyes (0).** 1 is the
    # route that removed the doubling a wearer saw in motion; 0 is the earlier
    # path that renders one eye per frame. Kept as an escape hatch for problems
    # specific to the both-eyes path (STEREO-DUAL-SUBMISSION-2026-10-10.md).
    # Read once when VR starts, as PREYVR_DUAL_STEREO.
    [int]$DualStereo=1,
    # Testing aid, not persisted: 0 off, 255 every layer of the in-game debug
    # overlay (rays, body slots, wrist test, foregrip; see dbg.draw).
    [ValidateRange(0,255)][int]$DebugOverlay=0,
    # Experimental, not persisted here (the VR Options row persists it): shots
    # leave the weapon's muzzle along the controller's aim (aim.shot 1).
    [switch]$MuzzleAim,
    [switch]$DryRun
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

function Resolve-PreyVRRenderSize {
    param($Saved, [int]$Width, [int]$Height, [bool]$WidthExplicit, [bool]$HeightExplicit)
    $DefaultWidth=$Width;$DefaultHeight=$Height
    if ($WidthExplicit -ne $HeightExplicit) {
        throw 'Set both -Width and -Height together; a partial override changes the eye aspect ratio.'
    }
    $migrate=$false
    if ($Saved -and !$WidthExplicit) {
        if ($Saved.PSObject.Properties['Width']) {$Width=[int]$Saved.Width}
        if ($Saved.PSObject.Properties['Height']) {$Height=[int]$Saved.Height}
        # v0.5.0 persisted its widescreen default without recording whether the
        # user chose it. Migrate only that exact unversioned pair. Explicit CLI
        # dimensions, custom saved sizes and versioned settings remain choices.
        # Version 2 does the same for the 2016 x 2160 default it replaced: also
        # saved without recording whether anyone chose it.
        $version=if ($Saved.PSObject.Properties['ResolutionDefaultsVersion']) {[int]$Saved.ResolutionDefaultsVersion} else {0}
        $hasSize=$Saved.PSObject.Properties['Width'] -and $Saved.PSObject.Properties['Height']
        $migrate=$hasSize -and (($version -lt 1 -and $Width -eq 2560 -and $Height -eq 1440) -or
                                ($version -lt 2 -and $Width -eq 2016 -and $Height -eq 2160))
        if ($migrate) {$Width=$DefaultWidth;$Height=$DefaultHeight}
    }
    if ($Width -lt 1280 -or $Width -gt 8192 -or $Height -lt 720 -or $Height -gt 8192) {
        throw 'Render size must be 1280..8192 by 720..8192.'
    }
    [pscustomobject]@{Width=$Width;Height=$Height;MigrateLegacy=$migrate}
}

function Resolve-PreyVRComfortSettings {
    param($Saved, $Overrides, [int]$SnapTurnDegrees, [int]$HeadRelativeMovement, [string]$ReferenceSpace)
    if ($Saved) {
        if (!$Overrides.ContainsKey('SnapTurnDegrees') -and $Saved.PSObject.Properties['SnapTurnDegrees']) {$SnapTurnDegrees=[int]$Saved.SnapTurnDegrees}
        if (!$Overrides.ContainsKey('HeadRelativeMovement') -and $Saved.PSObject.Properties['HeadRelativeMovement']) {$HeadRelativeMovement=[int]$Saved.HeadRelativeMovement}
        if (!$Overrides.ContainsKey('ReferenceSpace') -and $Saved.PSObject.Properties['ReferenceSpace']) {$ReferenceSpace=[string]$Saved.ReferenceSpace}
    }
    if ($SnapTurnDegrees -ne 0 -and ($SnapTurnDegrees -lt 15 -or $SnapTurnDegrees -gt 90)) {throw 'SnapTurnDegrees must be 0 (smooth) or 15..90.'}
    if ($HeadRelativeMovement -notin @(0,1)) {throw 'HeadRelativeMovement must be 0 or 1.'}
    if ($ReferenceSpace -notin @('auto','local')) {throw 'ReferenceSpace must be auto (prefer floor) or local.'}
    [pscustomobject]@{SnapTurnDegrees=$SnapTurnDegrees;HeadRelativeMovement=$HeadRelativeMovement;ReferenceSpace=$ReferenceSpace.ToLowerInvariant()}
}

function Resolve-PreyVRStereoSettings {
    param($Saved, $Overrides, [int]$DualStereo)
    if ($Saved -and !$Overrides.ContainsKey('DualStereo') -and $Saved.PSObject.Properties['DualStereo']) {$DualStereo=[int]$Saved.DualStereo}
    if ($DualStereo -notin @(0,1)) {throw 'DualStereo must be 1 (both eyes per frame) or 0 (alternate eyes).'}
    $DualStereo
}

function Get-PreyVRStereoStatus {
    param([string]$LogText)
    # VrMode logs one dual_stereo line per VR start, before state=active: on,
    # off (PREYVR_DUAL_STEREO=0 or 3D VIEW alternate), mono (3D VIEW mono) or
    # unavailable (hooks refused; alternate eyes).
    # A fallback is otherwise silent, so the launcher reports what came up.
    $found=[regex]::Matches($LogText,'preyvr_vr dual_stereo=([a-z]+)')
    if (!$found.Count) {return 'unknown'}
    $found[$found.Count-1].Groups[1].Value
}

function Get-PreyVRStartupCommands {
    param($Comfort, [int]$UiScalePercent, [int]$UiGuide, [int]$DebugOverlay=0, [bool]$MuzzleAim=$false)
    # One ordered batch, before enabling VR. Successive writes separated by a
    # guessed sleep can overwrite commands while the channel is busy.
    "move.snap $($Comfort.SnapTurnDegrees)"
    "move.headrelative $($Comfort.HeadRelativeMovement)"
    "ui.scale $UiScalePercent"
    "ui.guide $UiGuide"
    'input.hotkeys 1'
    if ($DebugOverlay) {"dbg.draw $DebugOverlay"}
    if ($MuzzleAim) {'aim.shot 1'}
    'vr.enable'
}

function Save-PreyVRConfiguration {
    param([string]$Path, $Saved, [hashtable]$Settings, [bool]$MigrateLegacy)
    if ($MigrateLegacy) {
        $backup=$Path+'.before-resolution-fix-'+[guid]::NewGuid().ToString('N')+'.bak'
        Copy-Item -LiteralPath $Path -Destination $backup -ErrorAction Stop
        Write-Host "Previous display settings backed up to $backup"
    }
    if (!$Saved) {$Saved=[pscustomobject]@{}}
    foreach ($key in $Settings.Keys) {$Saved | Add-Member -NotePropertyName $key -NotePropertyValue $Settings[$key] -Force}
    $Saved | Add-Member -NotePropertyName 'ResolutionDefaultsVersion' -NotePropertyValue 2 -Force
    $Saved | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $Path -Encoding UTF8
}

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
    $saved=$null
    if (Test-Path -LiteralPath $config) {
        $saved=Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
        if (!$GameExe -and $saved.PSObject.Properties['GameExe']) {$GameExe=$saved.GameExe}
        if (!$PSBoundParameters.ContainsKey('HudLayer') -and $saved.PSObject.Properties['HudLayer']) {$HudLayer=[int]$saved.HudLayer}
        if (!$PSBoundParameters.ContainsKey('UiCurveDegrees') -and $saved.PSObject.Properties['UiCurveDegrees']) {$UiCurveDegrees=[int]$saved.UiCurveDegrees}
        if (!$PSBoundParameters.ContainsKey('PointerHand') -and $saved.PSObject.Properties['PointerHand']) {$PointerHand=[int]$saved.PointerHand}
        if (!$PSBoundParameters.ContainsKey('UiScalePercent') -and $saved.PSObject.Properties['UiScalePercent']) {$UiScalePercent=[int]$saved.UiScalePercent}
        if (!$PSBoundParameters.ContainsKey('UiGuide') -and $saved.PSObject.Properties['UiGuide']) {$UiGuide=[int]$saved.UiGuide}
    }
    $renderSize=Resolve-PreyVRRenderSize -Saved $saved -Width $Width -Height $Height `
        -WidthExplicit $PSBoundParameters.ContainsKey('Width') -HeightExplicit $PSBoundParameters.ContainsKey('Height')
    $Width=$renderSize.Width;$Height=$renderSize.Height
    $comfort=Resolve-PreyVRComfortSettings $saved $PSBoundParameters $SnapTurnDegrees $HeadRelativeMovement $ReferenceSpace
    $DualStereo=Resolve-PreyVRStereoSettings $saved $PSBoundParameters $DualStereo
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
    Write-Host "Prey VR: $Width x $Height per eye; UI scale $UiScalePercent%; guide $(if($UiGuide){'on'}else{'off'}); stereo $(if($DualStereo){'both eyes per frame'}else{'alternate eyes'}); runtime: $(if($Runtime){$Runtime}else{'system OpenXR runtime'})"
    if ($renderSize.MigrateLegacy) {Write-Host "Replacing an earlier default render size with $($renderSize.Width) x $($renderSize.Height) per eye, about the headset's own density."}
    Write-Host "Mod binaries: $BinDir"
    Write-Host "Comfort: snap $($comfort.SnapTurnDegrees) degrees (0 = smooth); head-relative $($comfort.HeadRelativeMovement); reference $($comfort.ReferenceSpace)"
    Write-Host 'Controls: menu button = pause; right stick = navigate; A = select; B = back.'
    Write-Host 'Point + beam-hand trigger = click/drag; grips = tabs; X/Y = actions.'
    Write-Host 'F12 or both grips + left Y = reset view. Press A at the title/loading prompt.'
    Write-Host 'Tilde (the key below Esc) opens Prey''s own console, when focused.'
    if ($DryRun) {Write-Host 'Preflight passed. Nothing launched.';exit 0}
    Save-PreyVRConfiguration -Path $config -Saved $saved -MigrateLegacy $renderSize.MigrateLegacy -Settings `
        @{GameExe=$GameExe;Width=$Width;Height=$Height;HudLayer=$HudLayer;UiCurveDegrees=$UiCurveDegrees;PointerHand=$PointerHand;UiScalePercent=$UiScalePercent;UiGuide=$UiGuide;SnapTurnDegrees=$comfort.SnapTurnDegrees;HeadRelativeMovement=$comfort.HeadRelativeMovement;ReferenceSpace=$comfort.ReferenceSpace;DualStereo=$DualStereo}
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
    $info.EnvironmentVariables['PREYVR_REFERENCE_SPACE']=$comfort.ReferenceSpace
    $info.EnvironmentVariables['PREYVR_DUAL_STEREO']="$DualStereo"
    if ($PSBoundParameters.ContainsKey('DebugOverlay')) {$info.EnvironmentVariables['PREYVR_DEBUG_OVERLAY']="$DebugOverlay"}
    if ($MuzzleAim) {$info.EnvironmentVariables['PREYVR_MUZZLE_AIM']='1'}
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
    Get-PreyVRStartupCommands $comfort $UiScalePercent $UiGuide $DebugOverlay $MuzzleAim.IsPresent |
        Set-Content -LiteralPath (Join-Path $run 'commands.txt') -Encoding ASCII
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
    Write-Host "VR is ready. Diagnostics: $run"
    switch (Get-PreyVRStereoStatus $text) {
        'on' {Write-Host 'Stereo: both eyes from each frame.'}
        'off' {Write-Host 'Stereo: alternate eyes (DualStereo 0, or the 3D VIEW option).'}
        'mono' {Write-Host 'Stereo: mono, one picture in both eyes (the 3D VIEW option).'}
        'unavailable' {Write-Host 'Stereo: both-eyes rendering was unavailable; using alternate eyes. See the log.' -ForegroundColor Yellow}
        default {Write-Host 'Stereo: the log did not report a mode.' -ForegroundColor Yellow}
    }
    Write-Host ''
    Write-Host 'To adjust while playing, run "Tune Prey VR.cmd" beside this launcher'
    Write-Host 'and type e.g.  ui.scale 130   (20..200, current session only).'
    Write-Host 'For future launches, edit the saved settings in PreyVR.json.'
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    if (!$DryRun) {Read-Host 'Press Enter to close' | Out-Null}
    exit 1
}
