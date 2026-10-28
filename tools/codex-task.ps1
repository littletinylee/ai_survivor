param(
    [Parameter(Mandatory)][string]$TaskFile,
    [ValidateSet('auto','official','subrouter')][string]$Provider = 'auto',
    [string]$Model = 'gpt-6.1-sol'
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/provider-common.ps1"
$root = Split-Path $PSScriptRoot -Parent
$taskPath = (Resolve-Path -LiteralPath $TaskFile).Path
$task = Get-Content -LiteralPath $taskPath -Raw
if ([string]::IsNullOrWhiteSpace($task)) { throw 'Task file is empty' }
if ($Provider -in @('auto','official')) {
    Push-Location $root
    try { $lines = @($task | & codex exec -C $root --sandbox workspace-write --json -c 'model_provider=openai' -m $Model -) }
    finally { Pop-Location }
    $exit = $LASTEXITCODE
    $events = @($lines | ForEach-Object { try { $_ | ConvertFrom-Json } catch {} })
    $errors = @($events | Where-Object { $_.type -in @('error','turn.failed') })
    if ($exit -eq 0 -and $errors.Count -eq 0) { $lines; return }
    $failureText = @($errors | ForEach-Object { $_.message; $_.error.message }) -join "`n"
    $failure = [pscustomobject]@{Json=[pscustomobject]@{result=$failureText}; Stderr=''}
    if ($Provider -eq 'official' -or $errors.Count -eq 0 -or (Get-FailureClass $failure) -ne 'quota') {
        $lines
        throw 'Codex failed without an explicit quota signal; no provider switch.'
    }
    $checkpoint = Save-Checkpoint -Root $root -Data @{
        from_provider='codex-official'; to_provider='subrouter'; task_file=$taskPath
        branch=(Get-CurrentBranch $root); reason=$failureText
        created_utc=[DateTime]::UtcNow.ToString('o')
    }
    Write-Host "Codex quota exhausted; checkpoint: $checkpoint; one Subrouter attempt."
}
& "$PSScriptRoot/codex-subrouter.ps1" -TaskFile $taskPath -Model $Model
