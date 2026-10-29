param(
    [Parameter(Mandatory)][string]$TaskFile,
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$Model = 'gpt-6.1-sol'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'provider-common.ps1')
$root = Split-Path $PSScriptRoot -Parent
$taskPath = (Resolve-Path -LiteralPath $TaskFile).ProviderPath
$task = Get-Content -LiteralPath $taskPath -Raw
if ([string]::IsNullOrWhiteSpace($task)) { throw 'Task file is empty; not starting Codex.' }
if (-not (Get-Command codex -ErrorAction SilentlyContinue)) { throw 'codex CLI not found on PATH.' }

$cfg = Get-SubrouterConfig -Root $root
# Config overrides are per process (-c only); no global Codex config is written.
# The key is passed via the env var named in env_key, set only for this process.
$saved = Set-ProcessEnv -Values @{ SUBROUTER_API_KEY = $cfg.ApiKey }
Push-Location $root
try {
    # Task goes through stdin ('-') to avoid shell prompt quoting.
    $task | & codex exec -C $root --sandbox workspace-write --json `
        -c 'model_provider=subrouter' `
        -c 'model_providers.subrouter.name=Subrouter' `
        -c "model_providers.subrouter.base_url=$($cfg.BaseUrl)" `
        -c 'model_providers.subrouter.env_key=SUBROUTER_API_KEY' `
        -c 'model_providers.subrouter.wire_api=responses' `
        -c 'model_providers.subrouter.request_max_retries=0' `
        -c 'model_providers.subrouter.stream_max_retries=0' `
        -c 'model_reasoning_effort="medium"' `
        -m $Model -
    $exit = $LASTEXITCODE
} finally {
    Pop-Location
    Restore-ProcessEnv -Saved $saved
}
# Single attempt: no retry loop on failure.
if ($exit -ne 0) { throw "Codex Subrouter run failed (exit $exit); single attempt, inspect output, do not loop." }
