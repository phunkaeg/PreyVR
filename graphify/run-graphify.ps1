[CmdletBinding()]
param(
    [ValidateSet('openai', 'claude', 'gemini', 'kimi', 'deepseek')]
    [string]$Backend = 'openai',
    [switch]$Deep
)

$ErrorActionPreference = 'Stop'

$graphify = Get-Command graphify -ErrorAction SilentlyContinue
if ($null -eq $graphify) {
    throw 'graphify was not found on PATH.'
}

$requiredKey = switch ($Backend) {
    'openai' { 'OPENAI_API_KEY' }
    'claude' { 'ANTHROPIC_API_KEY' }
    'gemini' { 'GEMINI_API_KEY or GOOGLE_API_KEY' }
    'kimi' { 'MOONSHOT_API_KEY' }
    'deepseek' { 'DEEPSEEK_API_KEY' }
}

if ($Backend -eq 'gemini') {
    $hasKey = -not [string]::IsNullOrWhiteSpace($env:GEMINI_API_KEY) -or -not [string]::IsNullOrWhiteSpace($env:GOOGLE_API_KEY)
} else {
    $hasKey = -not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($requiredKey, 'Process'))
}
if (-not $hasKey) {
    throw "Missing $requiredKey for Graphify semantic document extraction."
}

$graphifyRoot = Split-Path -Parent $PSCommandPath
Push-Location $graphifyRoot
try {
    & $graphify.Source './corpus' --backend $Backend --wiki $(if ($Deep) { '--mode'; 'deep' })
    if ($LASTEXITCODE -ne 0) { throw "graphify exited with code $LASTEXITCODE" }
} finally {
    Pop-Location
}
