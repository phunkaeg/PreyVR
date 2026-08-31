<#
.SYNOPSIS
    Collects one step of the A2b IPD sweep, or summarises the whole sweep.

.DESCRIPTION
    A2b measures the same scene at several eye separations and asks whether the
    difference between the eyes scales with the separation. That is the test that
    tells a real 6DoF eye offset apart from noise, because a single stereo pair
    cannot: a small depth-varying disparity and a bit of temporal AA both look
    like "a low ratio and a small shift".

    Every capture lands in the same directory with a name that records only its
    frame index, so the mapping from dump to IPD has to be established *as the
    sweep runs*, not reconstructed afterwards from timestamps. -Collect moves the
    pair produced by one step into its own folder; -Summarize reads the folders
    back and builds the table.

    It prints numbers and checks only the two predictions that were written down
    in docs/LIVE_CAMERA_A1_PROTOCOL.md before the run -- monotonicity, and the
    zero-IPD control sitting at the noise floor. It does not invent a threshold
    for the absolute values, for the same reason the rest of the tooling does not.

.PARAMETER Path
    The capture directory the DLL writes to (PREYVR_CAPTURE_DIR).

.PARAMETER Label
    Which sweep step just ran: 000, 045, 064 or 085 (the IPD in millimetres).

.EXAMPLE
    ./tools/Invoke-PreyVRIpdSweep.ps1 -Path $env:TEMP\preyvr-a2b -Collect -Label 000

.EXAMPLE
    ./tools/Invoke-PreyVRIpdSweep.ps1 -Path $env:TEMP\preyvr-a2b -Summarize
#>
[CmdletBinding(DefaultParameterSetName = 'Collect')]
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(ParameterSetName = 'Collect')][switch]$Collect,
    [Parameter(ParameterSetName = 'Collect', Mandatory = $true)][string]$Label,
    [Parameter(ParameterSetName = 'Summarize')][switch]$Summarize,
    [string]$FrameDiff,
    [int]$MaxShift = 512
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $FrameDiff) {
    # Two build trees exist in this project; prefer whichever actually has the
    # binary rather than hard-coding one and failing confusingly.
    foreach ($candidate in @(
            'build/configure/Release/preyvr_frame_diff.exe',
            'build/headless/Release/preyvr_frame_diff.exe')) {
        if (Test-Path -LiteralPath $candidate) { $FrameDiff = $candidate; break }
    }
}
if (-not $FrameDiff -or -not (Test-Path -LiteralPath $FrameDiff)) {
    Write-Error 'preyvr_frame_diff not found - build it first'
    exit 1
}

$root = (Resolve-Path -LiteralPath $Path).Path

# Runs the differ on a pair and turns its key=value lines into an object. The
# shift scan is what separates a shear from parallax, so a pair that produces no
# shiftscan line is reported as such rather than silently summarised without it.
function Get-PairMetrics {
    param([string]$Left, [string]$Right)

    $output = & $FrameDiff $Left $Right --max-shift $MaxShift 2>&1
    $record = [ordered]@{ Ok = $false }
    foreach ($line in $output) {
        foreach ($token in ([string]$line -split '\s+')) {
            if ($token -match '^([A-Za-z]+)=(.+)$') {
                $record[$Matches[1]] = $Matches[2]
            }
        }
    }
    $record.Ok = $record.Contains('meanAbsolute') -and $record.Contains('bestShift')
    $record.Raw = ($output -join "`n")
    [pscustomobject]$record
}

if ($PSCmdlet.ParameterSetName -eq 'Collect') {
    $loose = @(Get-ChildItem -LiteralPath $root -Filter '*.pvrframe' -File |
        Sort-Object LastWriteTime)

    if ($loose.Count -ne 2) {
        # Deliberately strict. Three files means a capture from a previous step
        # was left behind, and guessing which two belong together is exactly the
        # silent mis-pairing this script exists to prevent.
        Write-Error ("expected exactly 2 loose captures in $root for step $Label, found " +
                     "$($loose.Count). Collect each step before running the next one.")
        exit 1
    }

    $destination = Join-Path $root ("ipd-" + $Label)
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    foreach ($file in $loose) {
        Move-Item -LiteralPath $file.FullName -Destination $destination -Force
    }

    $moved = @(Get-ChildItem -LiteralPath $destination -Filter '*.pvrframe' -File |
        Sort-Object Name)
    $metrics = Get-PairMetrics -Left $moved[0].FullName -Right $moved[1].FullName
    $metrics | Format-List | Out-String | Write-Host
    Write-Host "collected step $Label into $destination"
    return
}

# --- summary ---------------------------------------------------------------

$steps = @(Get-ChildItem -LiteralPath $root -Directory -Filter 'ipd-*' | Sort-Object Name)
if ($steps.Count -eq 0) {
    Write-Error "no ipd-* step folders in $root - run -Collect for each step first"
    exit 1
}

$rows = foreach ($step in $steps) {
    $pair = @(Get-ChildItem -LiteralPath $step.FullName -Filter '*.pvrframe' -File |
        Sort-Object Name)
    if ($pair.Count -ne 2) {
        Write-Warning "$($step.Name) does not hold exactly 2 captures - skipping"
        continue
    }
    $m = Get-PairMetrics -Left $pair[0].FullName -Right $pair[1].FullName
    if (-not $m.Ok) {
        Write-Warning "$($step.Name) produced no usable metrics - skipping"
        continue
    }
    [pscustomobject]@{
        IpdMillimetres = [int]($step.Name -replace '^ipd-', '')
        MeanAbsolute   = [double]$m.meanAbsolute
        ResidualAtZero = [double]$m.residualAtZero
        ResidualAtBest = [double]$m.residualAtBest
        BestShift      = [int]$m.bestShift
        Improvement    = [double]($m.improvement -replace 'x$', '')
        Verdict        = [string]$m.verdict
        ChangedRatio   = [double]$m.changedPixelRatio
    }
}
$rows = @($rows | Sort-Object IpdMillimetres)

$rows | Format-Table -AutoSize | Out-String | Write-Host

Write-Host '--- predictions recorded in docs/LIVE_CAMERA_A1_PROTOCOL.md before the run ---'

$control = @($rows | Where-Object { $_.IpdMillimetres -eq 0 })
$armed = @($rows | Where-Object { $_.IpdMillimetres -gt 0 })

if ($control.Count -eq 1) {
    # The control is the load-bearing row: with symmetric frusta and no eye
    # offset the two eyes are the same camera, so anything much above the A1
    # noise floor means the difference being measured is not the eye offset.
    $floor = $control[0].ResidualAtZero
    Write-Host ("control (0 mm) residualAtZero = {0:F4}; A1 noise floor was 0.0167" -f $floor)
    if ($armed.Count -gt 0) {
        $smallest = ($armed | Measure-Object -Property ResidualAtZero -Minimum).Minimum
        $separation = if ($floor -gt 0) { $smallest / $floor } else { [double]::PositiveInfinity }
        Write-Host ("separation from control to the smallest armed IPD = {0:F1}x" -f $separation)
    }
} else {
    Write-Host 'control (0 mm) step not present - the sweep cannot be judged without it'
}

if ($armed.Count -ge 2) {
    $monotonic = $true
    for ($i = 1; $i -lt $armed.Count; $i++) {
        if ($armed[$i].ResidualAtZero -le $armed[$i - 1].ResidualAtZero) { $monotonic = $false }
    }
    Write-Host ("monotonic in IPD: {0}" -f $(if ($monotonic) { 'yes' } else { 'NO' }))

    # Proportionality is the weaker claim and is printed rather than judged:
    # disparity saturates once it exceeds the local image structure, so the
    # larger separations are expected to come in under a straight line.
    $base = $armed[0]
    foreach ($row in $armed) {
        $ipdRatio = $row.IpdMillimetres / $base.IpdMillimetres
        $obsRatio = if ($base.ResidualAtZero -gt 0) { $row.ResidualAtZero / $base.ResidualAtZero } else { 0 }
        Write-Host ("  {0,3} mm: IPD x{1:F2}  residual x{2:F2}" -f $row.IpdMillimetres, $ipdRatio, $obsRatio)
    }
}

$shears = @($rows | Where-Object { $_.Verdict -eq 'uniform_shear_dominates' })
if ($shears.Count -gt 0) {
    Write-Host ''
    Write-Host ("WARNING: {0} step(s) report uniform_shear_dominates. With symmetric frusta" -f $shears.Count)
    Write-Host '         there should be no shear, so either SetStereoAsymmetry(1.0) did not'
    Write-Host '         take effect or the eye offset is reaching the projection instead of'
    Write-Host '         the view position.'
}

$clamped = @($rows | Where-Object { $_.Verdict -eq 'clamped_at_limit_widen_max_shift' })
if ($clamped.Count -gt 0) {
    Write-Host ''
    Write-Host ("WARNING: {0} step(s) hit the shift-search limit. Re-run with a larger" -f $clamped.Count)
    Write-Host '         -MaxShift; a clamped scan reports a floor, not a measurement.'
}
