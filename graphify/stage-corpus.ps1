[CmdletBinding()]
param(
    [string]$SharedCorpus = 'D:\Dev Debug\VR Modding\cross-engine-graph\corpus'
)

$ErrorActionPreference = 'Stop'

$graphifyRoot = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent $graphifyRoot
$corpusRoot = Join-Path $graphifyRoot 'corpus'
$preyDocs = Join-Path $repoRoot 'docs'

if (-not (Test-Path -LiteralPath $SharedCorpus -PathType Container)) {
    throw "Shared corpus not found: $SharedCorpus"
}

Get-ChildItem -LiteralPath $corpusRoot -Force |
    Where-Object { $_.Name -ne 'README.md' } |
    Remove-Item -Recurse -Force

New-Item -ItemType Directory -Path (Join-Path $corpusRoot 'preyvr') -Force | Out-Null
Get-ChildItem -LiteralPath $preyDocs -Filter '*.md' -File |
    Copy-Item -Destination (Join-Path $corpusRoot 'preyvr') -Force

Get-ChildItem -LiteralPath $SharedCorpus -Directory | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $corpusRoot -Recurse -Force
}

$summary = Get-ChildItem -LiteralPath $corpusRoot -Directory | ForEach-Object {
    $count = @(Get-ChildItem -LiteralPath $_.FullName -Filter '*.md' -File -Recurse).Count
    "  $($_.Name): $count markdown files"
}

Write-Host 'Staged Graphify corpus:'
$summary | Write-Host
