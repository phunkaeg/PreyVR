<#
.SYNOPSIS
    Composes two eye dumps into a single image that can be judged by eye.

.DESCRIPTION
    The virtual VR view. Once the double-render produces a left and a right
    frame, this turns them into something a person -- or a model reading the PNG
    -- can actually assess, with no headset, no OpenXR session and no submission
    path involved.

    That separation is the point. Per-eye rendering being *correct* and the XR
    swapchain being *plumbed* are independent problems, and this lets the first
    one be finished and verified before the second one starts.

    Three modes, because they answer different questions:

      SideBySide  Is each eye individually sane? Framing, culling, the viewmodel,
                  anything that is obviously broken in one eye only.

      Anaglyph    Is the stereo *correct*? Disparity shows up directly as colour
                  fringing: near objects fringe one way, far objects the other,
                  and anything at the convergence distance is grey. A swapped
                  pair, a zero IPD, or an inverted eye offset are all immediately
                  visible here and all nearly invisible side by side.

      Difference  How much disparity is there, spatially? Amplified |L-R|. Useful
                  for spotting that only part of the frame has parallax -- which
                  is what a viewmodel rendered from a single camera would look
                  like while the world behind it is correctly stereo.

.EXAMPLE
    ./tools/New-StereoView.ps1 -Left frame-100-tag0.pvrframe -Right frame-100-tag1.pvrframe -Mode Anaglyph
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Left,
    [Parameter(Mandatory = $true)][string]$Right,
    [ValidateSet('SideBySide', 'Anaglyph', 'Difference')][string]$Mode = 'SideBySide',
    [int]$MaxWidth = 1600,
    [int]$DifferenceGain = 4,
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'FrameDumpCommon.ps1')

$leftDump = Read-PreyVRFrameDump -Path (Resolve-Path -LiteralPath $Left)
$rightDump = Read-PreyVRFrameDump -Path (Resolve-Path -LiteralPath $Right)

if ($leftDump.Width -ne $rightDump.Width -or $leftDump.Height -ne $rightDump.Height) {
    # Reported rather than scaled to fit: two eyes of different sizes means
    # something is wrong upstream, and quietly resampling would hide it.
    throw "eye dumps differ in size: $($leftDump.Width)x$($leftDump.Height) vs $($rightDump.Width)x$($rightDump.Height)"
}
if ($leftDump.Tag -eq $rightDump.Tag) {
    Write-Warning "both dumps carry tag $($leftDump.Tag) - are these really two different eyes?"
}

$width = $leftDump.Width
$height = $leftDump.Height

function Get-PixelArray {
    param([System.Drawing.Bitmap]$Bitmap)
    $rect = New-Object System.Drawing.Rectangle(0, 0, $Bitmap.Width, $Bitmap.Height)
    $locked = $Bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $buffer = New-Object byte[] ($locked.Stride * $Bitmap.Height)
        [System.Runtime.InteropServices.Marshal]::Copy($locked.Scan0, $buffer, 0, $buffer.Length)
        return @{ Bytes = $buffer; Stride = $locked.Stride }
    }
    finally { $Bitmap.UnlockBits($locked) }
}

switch ($Mode) {
    'SideBySide' {
        $output = New-Object System.Drawing.Bitmap(($width * 2), $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($output)
        try {
            $graphics.DrawImage($leftDump.Bitmap, 0, 0, $width, $height)
            $graphics.DrawImage($rightDump.Bitmap, $width, 0, $width, $height)
            # A divider, so it is never ambiguous where one eye ends -- at a
            # glance two similar frames can look like one wide one.
            $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::Magenta, 2)
            $graphics.DrawLine($pen, $width, 0, $width, $height)
            $pen.Dispose()
        }
        finally { $graphics.Dispose() }
    }

    default {
        # Anaglyph and Difference both walk pixels, so they share the read.
        $l = Get-PixelArray -Bitmap $leftDump.Bitmap
        $r = Get-PixelArray -Bitmap $rightDump.Bitmap

        $output = New-Object System.Drawing.Bitmap($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $rect = New-Object System.Drawing.Rectangle(0, 0, $width, $height)
        $locked = $output.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $out = New-Object byte[] ($locked.Stride * $height)
            for ($y = 0; $y -lt $height; $y++) {
                $lRow = $y * $l.Stride
                $rRow = $y * $r.Stride
                $oRow = $y * $locked.Stride
                for ($x = 0; $x -lt $width; $x++) {
                    $lo = $lRow + ($x * 4)
                    $ro = $rRow + ($x * 4)
                    $oo = $oRow + ($x * 4)

                    if ($Mode -eq 'Anaglyph') {
                        # Red channel from the left eye, green and blue from the
                        # right. BGRA in memory, so index 2 is red.
                        $out[$oo]     = $r.Bytes[$ro]       # blue  <- right
                        $out[$oo + 1] = $r.Bytes[$ro + 1]   # green <- right
                        $out[$oo + 2] = $l.Bytes[$lo + 2]   # red   <- left
                    }
                    else {
                        # Amplified absolute difference, clamped. Gain makes small
                        # parallax visible; without it a correct stereo pair looks
                        # almost black and tells you nothing.
                        for ($c = 0; $c -lt 3; $c++) {
                            $d = [math]::Abs([int]$l.Bytes[$lo + $c] - [int]$r.Bytes[$ro + $c]) * $DifferenceGain
                            if ($d -gt 255) { $d = 255 }
                            $out[$oo + $c] = [byte]$d
                        }
                    }
                    $out[$oo + 3] = 255
                }
            }
            [System.Runtime.InteropServices.Marshal]::Copy($out, 0, $locked.Scan0, $out.Length)
        }
        finally { $output.UnlockBits($locked) }
    }
}

$final = Resize-PreyVRBitmap -Bitmap $output -MaxWidth $MaxWidth

if (-not $OutputPath) {
    $directory = Split-Path -Parent (Resolve-Path -LiteralPath $Left)
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($Left)
    $OutputPath = Join-Path $directory "$stem-$($Mode.ToLower()).png"
}
$final.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)

if (-not [object]::ReferenceEquals($final, $output)) { $final.Dispose() }
$output.Dispose()
$leftDump.Bitmap.Dispose()
$rightDump.Bitmap.Dispose()

Write-Output "preyvr_stereo_view mode=$Mode native=${width}x${height} leftTag=$($leftDump.Tag) rightTag=$($rightDump.Tag) target=$OutputPath"
