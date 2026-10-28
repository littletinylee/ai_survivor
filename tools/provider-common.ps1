# Shared helpers for provider routing. Dot-source from tools/*.ps1.
# Secrets are read at runtime from the ignored orchestration/local/subrouter.json and
# only ever set as process-scoped environment variables. Never echo or log them.

$script:SubrouterConfigRelPath = 'orchestration\local\subrouter.json'

function Get-SubrouterConfig {
    param([Parameter(Mandatory)][string]$Root)
    $path = Join-Path $Root $script:SubrouterConfigRelPath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw 'Subrouter config missing: orchestration/local/subrouter.json (contents not shown).'
    }
    try {
        $cfg = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    } catch {
        throw 'Subrouter config is not valid JSON (contents not shown).'
    }
    $apiKey = [string]$cfg.api_key
    if ([string]::IsNullOrWhiteSpace($apiKey)) {
        throw 'Subrouter api_key is empty; refusing to start.'
    }
    return [pscustomobject]@{
        BaseUrl = Assert-HttpsBaseUrl -Url ([string]$cfg.base_url)
        ApiKey  = $apiKey.Trim()
    }
}

function Assert-HttpsBaseUrl {
    param([Parameter(Mandatory)][string]$Url)
    $trimmed = $Url.Trim()
    $uri = $null
    if (-not [Uri]::TryCreate($trimmed, [UriKind]::Absolute, [ref]$uri) -or
        $uri.Scheme -ne 'https' -or [string]::IsNullOrEmpty($uri.Host) -or
        -not [string]::IsNullOrEmpty($uri.UserInfo)) {
        throw 'Subrouter base_url must be an absolute https URL without embedded credentials.'
    }
    return $trimmed.TrimEnd('/')
}

# Anthropic SDK appends /v1/messages itself, so the relay root must not end with /v1.
function ConvertTo-ClaudeRelayBase {
    param([Parameter(Mandatory)][string]$BaseUrl)
    return ($BaseUrl -replace '/v1$', '').TrimEnd('/')
}

# Sets process-scoped env vars. A $null value removes the variable for this process.
# Returns the previous values so Restore-ProcessEnv can put them back.
function Set-ProcessEnv {
    param([Parameter(Mandatory)][hashtable]$Values)
    $saved = @{}
    foreach ($name in $Values.Keys) {
        $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        if ($null -eq $Values[$name]) { Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue }
        else { [Environment]::SetEnvironmentVariable($name, $Values[$name], 'Process') }
    }
    return $saved
}

function Restore-ProcessEnv {
    param([Parameter(Mandatory)][hashtable]$Saved)
    foreach ($name in $Saved.Keys) {
        if ($null -eq $Saved[$name]) { Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue }
        else { [Environment]::SetEnvironmentVariable($name, $Saved[$name], 'Process') }
    }
}

# Parsed JSON from `claude -p --output-format json`, or $null when stdout is not JSON.
function Invoke-ClaudeOnce {
    param(
        [Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][string]$Task,
        [Parameter(Mandatory)][string]$ModelId,
        [Parameter(Mandatory)][double]$MaxBudgetUsd
    )
    if (-not (Get-Command claude -ErrorAction SilentlyContinue)) {
        throw 'claude CLI not found on PATH.'
    }
    # stderr goes to an ignored temp file so it does not mix into the JSON stdout.
    $errFile = Join-Path $Root ('orchestration\local\claude-stderr-' + [guid]::NewGuid().ToString('N') + '.txt')
    try {
        Push-Location $Root
        try {
            # --setting-sources project keeps user-level settings (for example a global relay env) out of this run.
            $lines = @(& claude --setting-sources project -p $Task --model $ModelId --max-budget-usd $MaxBudgetUsd --output-format json --tools 'Read,Edit,Write,Glob,Grep' --allowedTools 'Read,Edit,Write,Glob,Grep' 2> $errFile)
        } finally { Pop-Location }
        $exit = $LASTEXITCODE
        $stdout = (($lines | Out-String).Trim())
        $stderr = ''
        if (Test-Path -LiteralPath $errFile) { $stderr = (Get-Content -LiteralPath $errFile -Raw) }
    } finally {
        Remove-Item -LiteralPath $errFile -ErrorAction SilentlyContinue
    }
    $json = $null
    if (-not [string]::IsNullOrWhiteSpace($stdout)) {
        try { $json = $stdout | ConvertFrom-Json } catch { $json = $null }
    }
    return [pscustomobject]@{ ExitCode = $exit; Stdout = $stdout; Stderr = $stderr; Json = $json }
}

function Test-ClaudeSuccess {
    param([Parameter(Mandatory)]$Result)
    if ($Result.ExitCode -ne 0 -or $null -eq $Result.Json) { return $false }
    if ($Result.Json.subtype -and $Result.Json.subtype -ne 'success') { return $false }
    $isError = $Result.Json.PSObject.Properties['is_error']
    return -not ($isError -and $Result.Json.is_error -eq $true)
}

# Returns 'quota' only for an explicit quota/rate-limit signal with a parseable JSON result.
# Auth, budget, permission and other failures always return 'other' so they never trigger a switch.
function Get-FailureClass {
    param([Parameter(Mandatory)]$Result)
    if ($null -eq $Result.Json) { return 'other' }
    # Only inspect failure fields: normal JSON includes a permission_denials key,
    # which is not itself a permission error.
    $text = @($Result.Json.result, $Result.Json.api_error_status,
              $Result.Json.terminal_reason, $Result.Json.subtype, $Result.Stderr) -join "`n"
    $noSwitch = '(?i)\b(401|403)\b|authentication|unauthori[sz]ed|forbidden|invalid[ _-]?api[ _-]?key|budget|permission|denied'
    if ($text -match $noSwitch) { return 'other' }
    $quota = '(?i)\b429\b|rate[_ -]?limit|quota[ _-]?exhausted|insufficient_quota|usage[ _-]?limit|hit[ _-]?(your[ _-]?)?limit|too many requests'
    if ($text -match $quota) { return 'quota' }
    return 'other'
}

function Get-CurrentBranch {
    param([Parameter(Mandatory)][string]$Root)
    # Reads .git/HEAD directly; git is never executed.
    try {
        $gitPath = Join-Path $Root '.git'
        if (Test-Path -LiteralPath $gitPath -PathType Leaf) {
            $pointer = (Get-Content -LiteralPath $gitPath -Raw).Trim()
            if ($pointer -notmatch '^gitdir:\s*(.+)$') { return $null }
            $gitDir = $Matches[1]
            if (-not [IO.Path]::IsPathRooted($gitDir)) { $gitDir = Join-Path $Root $gitDir }
        } else {
            $gitDir = $gitPath
        }
        $head = (Get-Content -LiteralPath (Join-Path $gitDir 'HEAD') -Raw).Trim()
        if ($head -match '^ref:\s*refs/heads/(.+)$') { return $Matches[1] }
        return $null
    } catch {
        return $null
    }
}

function Save-Checkpoint {
    param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][hashtable]$Data)
    $dir = Join-Path $Root 'orchestration\local\checkpoints'
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $file = Join-Path $dir ('provider-task-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N') + '.json')
    $Data | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $file -Encoding UTF8
    return $file
}
