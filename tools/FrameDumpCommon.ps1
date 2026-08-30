<#
.SYNOPSIS
    Shared reader for .pvrframe backbuffer dumps.

.DESCRIPTION
    Dot-sourced by Convert-FrameDump.ps1 and New-StereoView.ps1 so the file
    format is described in exactly one place. A format contract duplicated across
    two readers is a format contract that drifts, and the drift shows up as
    plausible-looking wrong images.

    Must match include/preyvr/FrameDump.h.
#>

Set-StrictMode -Version Latest

Add-Type -AssemblyName System.Drawing

$script:PreyVRFrameHeaderSize = 48
$script:PreyVRFrameMagic = [System.Text.Encoding]::ASCII.GetBytes('PVRFRAME')

function Read-PreyVRFrameDump {
    <#
    .SYNOPSIS
        Reads a .pvrframe file into a header plus a GDI+ bitmap.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt $script:PreyVRFrameHeaderSize) {
        throw "$Path is shorter than a header"
    }
    for ($i = 0; $i -lt $script:PreyVRFrameMagic.Length; $i++) {
        if ($bytes[$i] -ne $script:PreyVRFrameMagic[$i]) {
            throw "$Path does not start with PVRFRAME"
        }
    }

    $version    = [BitConverter]::ToUInt32($bytes, 8)
    $width      = [BitConverter]::ToUInt32($bytes, 12)
    $height     = [BitConverter]::ToUInt32($bytes, 16)
    $format     = [BitConverter]::ToUInt32($bytes, 20)
    $rowPitch   = [BitConverter]::ToUInt32($bytes, 24)
    $tag        = [BitConverter]::ToUInt32($bytes, 28)
    $frameIndex = [BitConverter]::ToUInt64($bytes, 32)

    if ($version -ne 1) { throw "$Path has unsupported version $version" }

    $expected = [int64]$script:PreyVRFrameHeaderSize + ([int64]$rowPitch * [int64]$height)
    if ($bytes.LongLength -ne $expected) {
        # The realistic corruption is a capture interrupted mid-write.
        throw "$Path is $($bytes.LongLength) bytes, header implies $expected"
    }

    # 28 = DXGI_FORMAT_R8G8B8A8_UNORM (the live swapchain), 87 = B8G8R8A8_UNORM.
    # GDI+ Format32bppArgb is BGRA in memory, so only the first needs a swap.
    $swapRedAndBlue = switch ($format) {
        28 { $true }
        87 { $false }
        default { throw "$Path has unhandled DXGI format $format" }
    }

    $bitmap = New-Object System.Drawing.Bitmap([int]$width, [int]$height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $rect = New-Object System.Drawing.Rectangle(0, 0, [int]$width, [int]$height)
    $locked = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $row = New-Object byte[] ($width * 4)
        for ($y = 0; $y -lt $height; $y++) {
            [Array]::Copy($bytes, $script:PreyVRFrameHeaderSize + ($y * $rowPitch), $row, 0, $width * 4)
            if ($swapRedAndBlue) {
                for ($x = 0; $x -lt $row.Length; $x += 4) {
                    $r = $row[$x]; $row[$x] = $row[$x + 2]; $row[$x + 2] = $r
                }
            }
            # The backbuffer's alpha is not meaningful for a presented image, and
            # a zero alpha would render fully transparent.
            for ($x = 3; $x -lt $row.Length; $x += 4) { $row[$x] = 255 }
            [System.Runtime.InteropServices.Marshal]::Copy($row, 0, [IntPtr]::Add($locked.Scan0, $y * $locked.Stride), $row.Length)
        }
    }
    finally {
        $bitmap.UnlockBits($locked)
    }

    [pscustomobject]@{
        Path       = $Path
        Width      = [int]$width
        Height     = [int]$height
        Format     = [int]$format
        RowPitch   = [int]$rowPitch
        Tag        = [int]$tag
        FrameIndex = [uint64]$frameIndex
        Bitmap     = $bitmap
    }
}

function Resize-PreyVRBitmap {
    <#
    .SYNOPSIS
        Downscales to a maximum width, returning the original when no resize is needed.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][System.Drawing.Bitmap]$Bitmap,
        [Parameter(Mandatory = $true)][int]$MaxWidth
    )

    if ($MaxWidth -le 0 -or $Bitmap.Width -le $MaxWidth) { return $Bitmap }

    $scale = $MaxWidth / [double]$Bitmap.Width
    $newWidth = [int][math]::Round($Bitmap.Width * $scale)
    $newHeight = [int][math]::Round($Bitmap.Height * $scale)
    $resized = New-Object System.Drawing.Bitmap($newWidth, $newHeight)
    $graphics = [System.Drawing.Graphics]::FromImage($resized)
    try {
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.DrawImage($Bitmap, 0, 0, $newWidth, $newHeight)
    }
    finally { $graphics.Dispose() }
    return $resized
}
