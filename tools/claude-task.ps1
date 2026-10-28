param(
    [Parameter(Mandatory)][string]$TaskFile,
    [ValidateSet('haiku','sonnet','opus')][string]$Model = 'haiku',
    [ValidateRange(0.01,1.0)][double]$MaxBudgetUsd = 0.25
)
$ErrorActionPreference = 'Stop'
$task = Get-Content -LiteralPath $TaskFile -Raw
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    # Official Claude auth only; exclude the user's OpenRouter endpoint/model overrides.
    # File implementation only. Compilation and tests belong to the local validation stage.
    & claude --setting-sources project -p $task --model $Model --max-budget-usd $MaxBudgetUsd --output-format json --tools 'Read,Edit,Write,Glob,Grep' --allowedTools 'Read,Edit,Write,Glob,Grep'
    if ($LASTEXITCODE -ne 0) { throw "Claude task failed ($LASTEXITCODE); checkpoint and inspect, do not loop." }
} finally { Pop-Location }
