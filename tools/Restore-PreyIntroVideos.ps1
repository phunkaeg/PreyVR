<#
.SYNOPSIS
    Restores the four intro videos renamed on 2026-09-08, and verifies them.

.DESCRIPTION
    The owner renamed four startup videos from .bk2 to .bak so test cycles stop
    replaying them. See docs/GAME_FILE_CHANGES.md, which is the exception list to
    this project's rule that the installed game is never modified.

    Dry run by default. Nothing is renamed without -Apply, because this writes to
    the game install and every other tool here is read-only.

    Each file is checked by SHA-256 before being restored. A file that does not
    match was replaced or corrupted rather than merely renamed, and this refuses
    it rather than putting an unknown file back under the original name -- at
    which point Steam's "Verify integrity of game files" is the correct repair.

.EXAMPLE
    ./tools/Restore-PreyIntroVideos.ps1
    ./tools/Restore-PreyIntroVideos.ps1 -Apply
#>
[CmdletBinding()]
param(
    [string]$VideoDir = 'D:\SteamLibrary\steamapps\common\Prey\GameSDK\Videos',
    [switch]$Apply,
    # Restore a file whose hash does not match. Off by default: an unexpected
    # hash means the file is not the one that was renamed.
    [switch]$IgnoreHashMismatch
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$expected = @{
    'Ryzen_Bumper'                             = '59F7343B5EA4B4F1FFE7A733D11ECCB2E5FC51E2045523B299E15F99B39F534C'
    'ArkaneLogoAnim_Redux_1080p2997_ST-16LUFS' = 'F468331B6B64C271BD256E5B83F0E914E96E959BE7903987EC0CCDA4059F21F2'
    'Bethesda_logo_anim_white'                 = '57C07A612739541EDDEC4847D61E69C8EFD12F49442B4699B9582331C68F9CA3'
    'LegalScreens'                             = '5D0AADA41A0A3060762D36EE8955CCB31DA12B1336B0E08C2C5726990E6D8ADB'
}

if (-not (Test-Path -LiteralPath $VideoDir)) {
    Write-Error "video folder not found: $VideoDir"
    exit 1
}
if (Get-Process -Name Prey -ErrorAction SilentlyContinue) {
    Write-Error 'Prey is running. Close it first: the files may be open.'
    exit 1
}

Write-Host "folder: $VideoDir"
if (-not $Apply) { Write-Host 'DRY RUN -- pass -Apply to actually rename' }
Write-Host ''

$restored = 0
$skipped = 0
foreach ($stem in $expected.Keys | Sort-Object) {
    $bak = Join-Path $VideoDir "$stem.bak"
    $bk2 = Join-Path $VideoDir "$stem.bk2"

    if (Test-Path -LiteralPath $bk2) {
        Write-Host ("  already restored   {0}" -f $stem)
        continue
    }
    if (-not (Test-Path -LiteralPath $bak)) {
        Write-Host ("  MISSING BOTH       {0}  -- use Steam's Verify integrity of game files" -f $stem)
        $skipped++
        continue
    }

    $actual = (Get-FileHash -LiteralPath $bak -Algorithm SHA256).Hash
    if ($actual -ne $expected[$stem]) {
        if (-not $IgnoreHashMismatch) {
            Write-Host ("  HASH MISMATCH      {0}" -f $stem)
            Write-Host ("                     expected {0}" -f $expected[$stem])
            Write-Host ("                     actual   {0}" -f $actual)
            Write-Host  '                     refusing: this is not the file that was renamed'
            $skipped++
            continue
        }
        Write-Host ("  hash mismatch, restoring anyway (-IgnoreHashMismatch)  {0}" -f $stem)
    }

    if ($Apply) {
        Rename-Item -LiteralPath $bak -NewName "$stem.bk2"
        Write-Host ("  restored           {0}" -f $stem)
    } else {
        Write-Host ("  would restore      {0}" -f $stem)
    }
    $restored++
}

Write-Host ''
Write-Host ("{0} file(s) {1}, {2} skipped." -f $restored, $(if ($Apply) { 'restored' } else { 'would be restored' }), $skipped)
if ($restored -gt 0 -and $Apply) {
    Write-Host 'The intro videos will play again on the next launch.'
    Write-Host 'Update docs/GAME_FILE_CHANGES.md so the exception list stays true.'
}
