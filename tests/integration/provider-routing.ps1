$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. "$root/tools/provider-common.ps1"
$cases = @(
    @{result="You've hit your limit"; api_error_status=429; permission_denials=@(); expect='quota'},
    @{result='rate_limit_error'; expect='quota'},
    @{result='insufficient_quota'; expect='quota'},
    @{result='Forbidden'; api_error_status=403; expect='other'},
    @{result='Unauthorized'; api_error_status=401; expect='other'},
    @{result='max budget reached'; api_error_status=429; expect='other'},
    @{result='permission denied'; expect='other'},
    @{result='task failed'; expect='other'}
)
foreach ($case in $cases) {
    $result = [pscustomobject]@{Json=[pscustomobject]$case; Stdout=($case | ConvertTo-Json); Stderr=''; ExitCode=1}
    if ((Get-FailureClass $result) -ne $case.expect) { throw "Wrong route: $($case.result)" }
}
if ((Get-FailureClass ([pscustomobject]@{Json=$null})) -ne 'other') { throw 'Malformed JSON must not switch' }
if ((ConvertTo-ClaudeRelayBase 'https://gateway.example/v1') -ne 'https://gateway.example') { throw 'Wrong Anthropic base path' }
$before = [Environment]::GetEnvironmentVariable('SUBROUTER_TEST_ENV','Process')
$saved = Set-ProcessEnv @{SUBROUTER_TEST_ENV='test'}
Restore-ProcessEnv $saved
if ([Environment]::GetEnvironmentVariable('SUBROUTER_TEST_ENV','Process') -ne $before) { throw 'Environment not restored' }
$budgetResult = [pscustomobject]@{ExitCode=0; Json=[pscustomobject]@{is_error=$false; subtype='error_max_budget_usd'}}
if (Test-ClaudeSuccess $budgetResult) { throw 'Budget exhaustion misreported as success' }
Write-Host 'Provider routing: 12 checks PASS (no network or secrets).'

# Execute the real wrappers against mock CLIs in a disposable fixture.
$fixture = Join-Path $root ('build/provider-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force "$fixture/tools", "$fixture/orchestration/local" | Out-Null
Copy-Item "$root/tools/provider-common.ps1", "$root/tools/claude-task.ps1", "$root/tools/codex-task.ps1", "$root/tools/codex-subrouter.ps1" "$fixture/tools"
'{"base_url":"https://gateway.example/v1","api_key":"DUMMY-NOT-A-REAL-KEY"}' | Set-Content "$fixture/orchestration/local/subrouter.json"
'Mock task only' | Set-Content "$fixture/task.txt"
$global:MockCalls = 0
$global:MockMode = 'quota'
function global:claude {
    $global:MockCalls++
    if ($global:MockCalls -eq 1) {
        $global:LASTEXITCODE = 1
        if ($global:MockMode -eq 'quota') { '{"is_error":true,"result":"usage limit reached","permission_denials":[],"api_error_status":429}' }
        else { '{"is_error":true,"result":"Forbidden","api_error_status":403}' }
    } else {
        if ($env:ANTHROPIC_API_KEY -ne 'DUMMY-NOT-A-REAL-KEY') { throw 'Relay env missing' }
        $global:LASTEXITCODE = 0
        '{"is_error":false,"result":"MOCK_READY"}'
    }
}
function global:codex {
    $global:MockCalls++
    if ($global:MockCalls -eq 1) {
        $global:LASTEXITCODE=1
        '{"type":"turn.failed","error":{"message":"usage limit reached"}}'
    } else {
        if ($env:SUBROUTER_API_KEY -ne 'DUMMY-NOT-A-REAL-KEY') { throw 'Codex relay env missing' }
        $global:LASTEXITCODE=0
        '{"type":"turn.completed"}'
    }
}
try {
    $keyBefore = $env:ANTHROPIC_API_KEY
    & "$fixture/tools/claude-task.ps1" -TaskFile "$fixture/task.txt" | Out-Null
    if ($global:MockCalls -ne 2 -or $env:ANTHROPIC_API_KEY -ne $keyBefore) { throw 'Claude fallback/environment failure' }
    $global:MockCalls=0; $global:MockMode='auth'
    $rejected=$false
    try { & "$fixture/tools/claude-task.ps1" -TaskFile "$fixture/task.txt" | Out-Null } catch { $rejected=$true }
    if (!$rejected -or $global:MockCalls -ne 1) { throw 'Auth failure switched providers' }
    $global:MockCalls=0
    $keyBefore=$env:SUBROUTER_API_KEY
    & "$fixture/tools/codex-task.ps1" -TaskFile "$fixture/task.txt" | Out-Null
    if ($global:MockCalls -ne 2 -or $env:SUBROUTER_API_KEY -ne $keyBefore) { throw 'Codex fallback/environment failure' }
    if (!(Get-ChildItem "$fixture/orchestration/local/checkpoints" -File)) { throw 'Missing checkpoint' }
    Write-Host 'Mock CLI handoff: Claude quota, auth stop, Codex quota, checkpoints and environment restoration PASS.'
} finally {
    Remove-Item Function:claude, Function:codex -ErrorAction SilentlyContinue
}
