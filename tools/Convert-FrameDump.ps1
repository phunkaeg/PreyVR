<#
.SYNOPSIS
    Converts a .pvrframe backbuffer dump into a PNG.

.DESCRIPTION
    The DLL writes raw mapped rows rather than an encoded image, so the capture
    path stays small and dependency-free inside the game process. Encoding is
    done out here instead, where System.Drawing is already available and a
    mistake costs nothing.

    The PNG exists to be *looked at*. Numeric comparison is done on the
    .pvrframe files themselves by preyvr_frame_diff, which reads the exact bytes
    that were mapped -- so nothing about this conversion, including the
    downscale, can affect a measurement.

.PARAMETER Path
    A .pvrframe file, or a directory of them.

.PARAMETER MaxWidth
    Longest edge of the output. Defaults to 1280 because a 2560x1440 PNG is
    unwieldy to view and this image is only ever for human (or model) eyes. Pass
    0 to keep native resolution.

.EXAMPLE
    ./tools/Convert-FrameDump.ps1 -Path frame-4210-tag0.pvrframe
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [int]$MaxWidth = 1280,
    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

# Must match include/preyvr/FrameDump.h. The header is a stated contract rather
# than a struct dump precisely so a second reader like this one can exist.
$HeaderSize = 48
$Magic = [System.Text.Encoding]::ASCII.GetBytes('PVRFRAME')

function Convert-OneDump {
    param([string]$File, [string]$OutDir, [int]$MaxEdge)

    $bytes = [System.IO.File]::ReadAllBytes($File)
    if ($bytes.Length -lt $HeaderSize) {
        Write-Error "$File is shorter than a header"
        return
    }
    for ($i = 0; $i -lt $Magic.Length; $i++) {
        if ($bytes[$i] -ne $Magic[$i]) {
            Write-Error "$File does not start with PVRFRAME"
            return
        }
    }

    $version   = [BitConverter]::ToUInt32($bytes, 8)
    $width     = [BitConverter]::ToUInt32($bytes, 12)
    $height    = [BitConverter]::ToUInt32($bytes, 16)
    $format    = [BitConverter]::ToUInt32($bytes, 20)
    $rowPitch  = [BitConverter]::ToUInt32($bytes, 24)

    if ($version -ne 1) { Write-Error "$File has unsupported version $version"; return }

    $expected = [int64]$HeaderSize + ([int64]$rowPitch * [int64]$height)
    if ($bytes.LongLength -ne $expected) {
        # The realistic corruption is a capture interrupted mid-write, so this is
        # checked rather than assumed.
        Write-Error "$File is $($bytes.LongLength) bytes, header implies $expected"
        return
    }

    # 28 = DXGI_FORMAT_R8G8B8A8_UNORM (the live swapchain), 87 = B8G8R8A8_UNORM.
    # GDI+ Format32bppArgb is BGRA in memory, so only the first needs a swap.
    $swapRedAndBlue = switch ($format) {
        28 { $true }
        87 { $false }
        default { Write-Error "$File has unhandled DXGI format $format"; return }
    }

    $bitmap = New-Object System.Drawing.Bitmap([int]$width, [int]$height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $rect = New-Object System.Drawing.Rectangle(0, 0, [int]$width, [int]$height)
    $locked = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $row = New-Object byte[] ($width * 4)
        for ($y = 0; $y -lt $height; $y++) {
            $sourceOffset = $HeaderSize + ($y * $rowPitch)
            [Array]::Copy($bytes, $sourceOffset, $row, 0, $width * 4)
            if ($swapRedAndBlue) {
                for ($x = 0; $x -lt $row.Length; $x += 4) {
                    $r = $row[$x]
                    $row[$x] = $row[$x + 2]
                    $row[$x + 2] = $r
                }
            }
            # The backbuffer's alpha is not meaningful for a presented image and
            # a zero alpha would render as fully transparent.
            for ($x = 3; $x -lt $row.Length; $x += 4) { $row[$x] = 255 }
            [System.Runtime.InteropServices.Marshal]::Copy($row, 0, [IntPtr]::Add($locked.Scan0, $y * $locked.Stride), $row.Length)
        }
    }
    finally {
        $bitmap.UnlockBits($locked)
    }

    $output = $bitmap
    if ($MaxEdge -gt 0 -and $width -gt $MaxEdge) {
        $scale = $MaxEdge / [double]$width
        $newWidth = [int][math]::Round($width * $scale)
        $newHeight = [int][math]::Round($height * $scale)
        $resized = New-Object System.Drawing.Bitmap($newWidth, $newHeight)
        $graphics = [System.Drawing.Graphics]::FromImage($resized)
        try {
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.DrawImage($bitmap, 0, 0, $newWidth, $newHeight)
        }
        finally { $graphics.Dispose() }
        $output = $resized
    }

    $target = Join-Path $OutDir ([System.IO.Path]::GetFileNameWithoutExtension($File) + '.png')
    $output.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
    if (-not [object]::ReferenceEquals($output, $bitmap)) { $output.Dispose() }
    $bitmap.Dispose()

    Write-Output "preyvr_frame_png source=$File target=$target native=${width}x${height} format=$format"
}

$resolved = Resolve-Path -LiteralPath $Path
$files = if (Test-Path -LiteralPath $resolved -PathType Container) {
    Get-ChildItem -LiteralPath $resolved -Filter '*.pvrframe' | ForEach-Object { $_.FullName }
} else {
    @([string]$resolved)
}

if ($files.Count -eq 0) { Write-Error "no .pvrframe files found at $Path"; exit 1 }

foreach ($file in $files) {
    $outDir = if ($OutputDirectory) { $OutputDirectory } else { Split-Path -Parent $file }
    if (-not (Test-Path -LiteralPath $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }
    Convert-OneDump -File $file -OutDir $outDir -MaxEdge $MaxWidth
}
