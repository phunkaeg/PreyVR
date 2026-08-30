<#
.SYNOPSIS
    Turns a directory of .pvrframe captures into the numbers and images a
    protocol run needs to be judged.

.DESCRIPTION
    A protocol run leaves a pile of dumps. This produces the separation table the
    A1 and A2 acceptance criteria are stated in terms of, plus the PNGs and
    stereo views, so the decision is made from one output rather than by running
    four tools by hand and holding the results in your head.

    It reports numbers and does not pass a verdict. No acceptance threshold has
    been derived yet -- that is the point of the noise-floor row -- and a script
    that printed PASS based on an invented constant would be worse than one that
    prints nothing, because it would look like evidence.

.PARAMETER Path
    Directory containing .pvrframe captures.

.EXAMPLE
    ./tools/Invoke-PreyVRStereoAnalysis.ps1 -Path "$env:LOCALAPPDATA\PreyVR\captures"
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string]$FrameDiff = "build/headless/Release/preyvr_frame_diff.exe",
    [switch]$SkipImages
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$resolved = Resolve-Path -LiteralPath $Path
$dumps = @(Get-ChildItem -LiteralPath $resolved -Filter '*.pvrframe' | Sort-Object Name)
if ($dumps.Count -lt 2) {
    Write-Error "need at least two .pvrframe captures in $Path, found $($dumps.Count)"
    exit 1
}

if (-not (Test-Path -LiteralPath $FrameDiff)) {
    Write-Error "preyvr_frame_diff not found at $FrameDiff - build it first"
    exit 1
}

# Parse frame index and tag out of the filenames the DLL writes:
# frame-<index>-tag<n>.pvrframe
$parsed = foreach ($d in $dumps) {
    if ($d.Name -match '^frame-(\d+)-tag(\d+)\.pvrframe$') {
        [pscustomobject]@{
            File  = $d.FullName
            Name  = $d.Name
            Frame = [uint64]$Matches[1]
            Tag   = [int]$Matches[2]
        }
    }
}
$parsed = @($parsed | Sort-Object Frame)

if ($parsed.Count -lt 2) {
    Write-Error "no captures matched the frame-<index>-tag<n>.pvrframe naming"
    exit 1
}

function Get-Diff {
    param([string]$A, [string]$B)
    $line = & $FrameDiff $A $B 2>&1 | Select-Object -First 1
    if ($line -notmatch 'result=ok') {
        return [pscustomobject]@{ Ok = $false; Raw = [string]$line }
    }
    $mean = if ($line -match 'meanAbsolute=([0-9.]+)') { [double]$Matches[1] } else { [double]::NaN }
    $max = if ($line -match 'maxAbsolute=(\d+)') { [int]$Matches[1] } else { -1 }
    $ratio = if ($line -match 'changedPixelRatio=([0-9.]+)') { [double]$Matches[1] } else { [double]::NaN }
    [pscustomobject]@{ Ok = $true; Mean = $mean; Max = $max; ChangedRatio = $ratio; Raw = [string]$line }
}

Write-Output ""
Write-Output "captures found: $($parsed.Count)"
$parsed | ForEach-Object { Write-Output ("  frame {0,-10} tag {1}  {2}" -f $_.Frame, $_.Tag, $_.Name) }

# **Non-overlapping** pairs: the protocols capture in twos, so dumps 1-2 are one
# experiment and 3-4 the next. Sliding a window over every adjacent pair would
# also compare the last dump of one experiment against the first of the next,
# producing a row that looks like a huge noise floor and is actually meaningless.
# Same tag on both sides is a noise-floor row; differing tags is a separation row.
$rows = @()
for ($i = 0; $i + 1 -lt $parsed.Count; $i += 2) {
    $a = $parsed[$i]
    $b = $parsed[$i + 1]
    $d = Get-Diff -A $a.File -B $b.File
    $kind = if ($a.Tag -eq $b.Tag) { 'noise floor' } else { "separation (tag $($a.Tag) vs $($b.Tag))" }
    $rows += [pscustomobject]@{
        Kind         = $kind
        Pair         = "$($a.Frame) -> $($b.Frame)"
        Mean         = if ($d.Ok) { [math]::Round($d.Mean, 6) } else { 'n/a' }
        Max          = if ($d.Ok) { $d.Max } else { 'n/a' }
        ChangedRatio = if ($d.Ok) { [math]::Round($d.ChangedRatio, 6) } else { 'n/a' }
        Note         = if ($d.Ok) { '' } else { $d.Raw }
    }
}

Write-Output ""
Write-Output "separation table"
$rows | Format-Table -AutoSize | Out-String | Write-Output

$floors = @($rows | Where-Object { $_.Kind -eq 'noise floor' -and $_.Mean -ne 'n/a' })
$seps = @($rows | Where-Object { $_.Kind -ne 'noise floor' -and $_.Mean -ne 'n/a' })

if ($floors.Count -eq 0) {
    Write-Warning "no same-tag pair captured, so there is no noise floor to compare against - the separation numbers above cannot be interpreted on their own"
} elseif ($seps.Count -gt 0) {
    $worstFloor = ($floors | Measure-Object -Property Mean -Maximum).Maximum
    $bestSep = ($seps | Measure-Object -Property Mean -Minimum).Minimum
    Write-Output ("noise floor (worst): {0}" -f $worstFloor)
    Write-Output ("separation (weakest): {0}" -f $bestSep)
    if ($worstFloor -gt 0) {
        Write-Output ("ratio: {0:N1}x" -f ($bestSep / $worstFloor))
    }
    Write-Output ""
    Write-Output "No threshold is applied. Compare the ratio against the separation table in"
    Write-Output "include/preyvr/FrameDump.h - and if it is marginal, investigate rather than"
    Write-Output "widening anything."
}

if (-not $SkipImages) {
    Write-Output ""
    & (Join-Path $PSScriptRoot 'Convert-FrameDump.ps1') -Path $resolved | Out-Null
    Write-Output "PNGs written beside the dumps"

    # Any pair tagged 0 then 1 is an eye pair worth viewing in stereo.
    for ($i = 0; $i + 1 -lt $parsed.Count; $i += 2) {
        if ($parsed[$i].Tag -eq 0 -and $parsed[$i + 1].Tag -eq 1) {
            foreach ($mode in 'Anaglyph', 'Difference') {
                & (Join-Path $PSScriptRoot 'New-StereoView.ps1') `
                    -Left $parsed[$i].File -Right $parsed[$i + 1].File -Mode $mode | Write-Output
            }
        }
    }
}
