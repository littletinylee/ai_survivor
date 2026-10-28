param(
    [Parameter(Mandatory)][string]$TaskFile,
    [ValidateSet('haiku','sonnet','opus')][string]$Model = 'haiku',
    [ValidateRange(0.01,1.0)][double]$MaxBudgetUsd = 0.25,
    # official: Claude official auth only. subrouter: relay from ignored local config.
    # auto: official first, then one Subrouter attempt only on an explicit quota/rate-limit signal.
    [ValidateSet('official','subrouter','auto')][string]$Provider = 'auto'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'provider-common.ps1')
$root = Split-Path $PSScriptRoot -Parent
$taskPath = (Resolve-Path -LiteralPath $TaskFile).ProviderPath
$task = Get-Content -LiteralPath $taskPath -Raw
if ([string]::IsNullOrWhiteSpace($task)) { throw 'Task file is empty; not starting Claude.' }

# File implementation only. Compilation and tests belong to the local validation stage.
$modelIds = @{ haiku = 'claude-haiku-5-5'; sonnet = 'claude-sonnet-5-5'; opus = 'claude-opus-5-5' }

if ($Provider -in @('official', 'auto')) {
    $official = Invoke-ClaudeOnce -Root $root -Task $task -ModelId $Model -MaxBudgetUsd $MaxBudgetUsd
    if (Test-ClaudeSuccess -Result $official) {
        Write-Output $official.Stdout
        return
    }
    if ($Provider -eq 'official' -or $null -eq $official.Json -or (Get-FailureClass -Result $official) -ne 'quota') {
        Write-Output $official.Stdout
        throw "Claude official run failed (exit $($official.ExitCode)); no provider switch. Inspect the result above; do not loop."
    }
    $checkpoint = Save-Checkpoint -Root $root -Data @{
        created_utc     = (Get-Date).ToUniversalTime().ToString('o')
        from_provider   = 'official'
        to_provider     = 'subrouter'
        task_file       = $taskPath
        model           = $Model
        fallback_model  = $modelIds[$Model]
        branch          = (Get-CurrentBranch -Root $root)
        reason          = 'explicit quota/rate-limit signal in official result'
        previous_result = @{
            exit_code = $official.ExitCode
            stdout    = $official.Stdout.Substring(0, [Math]::Min(4000, $official.Stdout.Length))
        }
    }
    Write-Host "Official quota signal detected. Checkpoint: $checkpoint. Trying Subrouter once."
}

# Subrouter path: process-only env, restored in finally. Never echo the key.
$cfg = Get-SubrouterConfig -Root $root
$relayBase = ConvertTo-ClaudeRelayBase -BaseUrl $cfg.BaseUrl
$saved = Set-ProcessEnv -Values @{
    ANTHROPIC_BASE_URL   = $relayBase
    ANTHROPIC_API_KEY    = $cfg.ApiKey
    ANTHROPIC_AUTH_TOKEN = $null
}
try {
    $sub = Invoke-ClaudeOnce -Root $root -Task $task -ModelId $modelIds[$Model] -MaxBudgetUsd $MaxBudgetUsd
} finally {
    Restore-ProcessEnv -Saved $saved
}
Write-Output $sub.Stdout
if (-not (Test-ClaudeSuccess -Result $sub)) {
    throw "Subrouter Claude run failed (exit $($sub.ExitCode)); single attempt, no further fallback."
}
