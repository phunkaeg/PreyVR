[CmdletBinding()]
param(
    [string]$SharedCorpus = 'D:\Dev Debug\VR Modding\cross-engine-graph\corpus'
)

$ErrorActionPreference = 'Stop'

$graphifyRoot = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent $graphifyRoot
$corpusRoot = Join-Path $graphifyRoot 'corpus'

if (-not (Test-Path -LiteralPath $SharedCorpus -PathType Container)) {
    throw "Shared corpus not found: $SharedCorpus"
}

# Destructive by design: the corpus is a staging area, fully rebuilt each run and
# gitignored apart from its README. Nothing unique lives here.
Get-ChildItem -LiteralPath $corpusRoot -Force |
    Where-Object { $_.Name -ne 'README.md' } |
    Remove-Item -Recurse -Force

$preyvrDir = Join-Path $corpusRoot 'preyvr'
New-Item -ItemType Directory -Path $preyvrDir -Force | Out-Null

# Directories that must never be staged.
#   build/              - generated, and enormous
#   graphify/           - the corpus itself; staging it would recurse
#   tools/Chairloader-* - third-party reference (1,131 headers, 16 MB). It is the
#                         PDB bridge we translate *from*, not this project's work,
#                         and it would swamp every other signal in the graph.
#   captures/rdc,frames - binary capture payloads
$excludedPattern = '\\(build|graphify|\.git|\.vs)\\|\\tools\\Chairloader'

function Copy-Staged {
    param(
        [Parameter(Mandatory)] [System.IO.FileInfo[]]$Files,
        [Parameter(Mandatory)] [string]$Label
    )
    $kept = 0
    foreach ($file in $Files) {
        if ($file.FullName -match $excludedPattern) { continue }
        # Flatten into the corpus while keeping provenance readable: docs/FOO.md
        # becomes docs__FOO.md, so two READMEs from different directories cannot
        # collide and the origin is still visible in the graph.
        $relative = $file.FullName.Substring($repoRoot.Length).TrimStart('\')
        $flat = ($relative -replace '[\\/]', '__')
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $preyvrDir $flat) -Force
        $kept++
    }
    Write-Host ("  {0,-28} {1} files" -f $Label, $kept)
}

Write-Host 'Staging PreyVR corpus:'

# Every markdown file in the repo, recursively -- docs, capture traces, the root
# README/AGENTS/CLAUDE guides, and the per-directory READMEs.
Copy-Staged -Label 'markdown (recursive)' -Files @(
    Get-ChildItem -LiteralPath $repoRoot -Filter '*.md' -File -Recurse
)

# Implementation and tests. Header and source alike: the layout headers carry the
# same verified offsets the documents describe, so they are the join between the
# research notes and the code.
Copy-Staged -Label 'C++ source and headers' -Files @(
    Get-ChildItem -LiteralPath $repoRoot -File -Recurse -Include '*.cpp', '*.h', '*.hpp'
)

# Build definition and the research helper scripts.
Copy-Staged -Label 'build and tooling' -Files @(
    Get-ChildItem -LiteralPath $repoRoot -File -Recurse -Include 'CMakeLists.txt', '*.ps1', '*.genny'
)

Get-ChildItem -LiteralPath $SharedCorpus -Directory | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $corpusRoot -Recurse -Force
}

Write-Host ''
Write-Host 'Staged corpus totals:'
Get-ChildItem -LiteralPath $corpusRoot -Directory | ForEach-Object {
    $count = @(Get-ChildItem -LiteralPath $_.FullName -File -Recurse).Count
    Write-Host ("  {0,-14} {1} files" -f $_.Name, $count)
}
