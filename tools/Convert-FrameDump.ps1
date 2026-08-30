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

    For two eyes at once, see New-StereoView.ps1.

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

. (Join-Path $PSScriptRoot 'FrameDumpCommon.ps1')

$resolved = Resolve-Path -LiteralPath $Path
# Wrapped in @() deliberately: PowerShell unwraps a single-element array on
# assignment, so without this a one-file argument yields a bare string and the
# .Count below throws under StrictMode. Only reproduces on the single-file path.
$files = @(
    if (Test-Path -LiteralPath $resolved -PathType Container) {
        Get-ChildItem -LiteralPath $resolved -Filter '*.pvrframe' | ForEach-Object { $_.FullName }
    } else {
        [string]$resolved
    }
)

if ($files.Count -eq 0) { Write-Error "no .pvrframe files found at $Path"; exit 1 }

foreach ($file in $files) {
    $outDir = if ($OutputDirectory) { $OutputDirectory } else { Split-Path -Parent $file }
    if (-not (Test-Path -LiteralPath $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

    $dump = Read-PreyVRFrameDump -Path $file
    try {
        $output = Resize-PreyVRBitmap -Bitmap $dump.Bitmap -MaxWidth $MaxWidth
        $target = Join-Path $outDir ([System.IO.Path]::GetFileNameWithoutExtension($file) + '.png')
        $output.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
        if (-not [object]::ReferenceEquals($output, $dump.Bitmap)) { $output.Dispose() }
        Write-Output "preyvr_frame_png source=$file target=$target native=$($dump.Width)x$($dump.Height) format=$($dump.Format) tag=$($dump.Tag)"
    }
    finally {
        $dump.Bitmap.Dispose()
    }
}
