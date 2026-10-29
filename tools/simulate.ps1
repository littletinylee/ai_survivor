param(
    [ValidateRange(0, 4294967295)][long]$Seed = 1,
    [ValidateSet('kite', 'stationary')][string]$Policy = 'kite',
    [ValidatePattern('^[A-Za-z0-9_]+$')][string]$CharacterId = 'deepseek_big_fish',
    [string]$Godot = "$PSScriptRoot/../.tools/godot-4.7.2/Godot_v4.7.2-stable_win64_console.exe",
    [switch]$RequireVictory
)
$ErrorActionPreference = 'Stop'
# Read-only against the project: run tools/test.ps1 first so the import step has happened.
$root = Split-Path $PSScriptRoot -Parent
$Godot = (Resolve-Path -LiteralPath $Godot).Path
$version = & $Godot --version
if ($LASTEXITCODE -ne 0 -or $version -ne '4.7.2.stable.official.ed1daf0bf') { throw "Unexpected Godot: $version" }
New-Item -ItemType Directory -Force "$root/build/test-results" | Out-Null

$name = "simulation-$Seed-$Policy"
$stdout = "$root/build/test-results/$name.out.log"
$stderr = "$root/build/test-results/$name.err.log"
$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $Godot
$startInfo.UseShellExecute = $false
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$startInfo.CreateNoWindow = $true
foreach ($argument in @('--headless', '--path', $root, '--', '--simulate-run',
        '--seed', "$Seed", '--policy', $Policy, '--character', $CharacterId)) {
    $startInfo.ArgumentList.Add($argument)
}
$process = [System.Diagnostics.Process]::new()
$process.StartInfo = $startInfo
if (-not $process.Start()) { throw "$name failed to start" }
$stdoutTask = $process.StandardOutput.ReadToEndAsync()
$stderrTask = $process.StandardError.ReadToEndAsync()
$timedOut = -not $process.WaitForExit(180000)
if ($timedOut) {
    $process.Kill($true)
    $process.WaitForExit()
}
$stdoutText = $stdoutTask.GetAwaiter().GetResult()
$stderrText = $stderrTask.GetAwaiter().GetResult()
Set-Content -LiteralPath $stdout -Value $stdoutText -NoNewline
Set-Content -LiteralPath $stderr -Value $stderrText -NoNewline
$exitCode = $process.ExitCode
$process.Dispose()
$output = $stdoutText + $stderrText
Write-Host $output
if ($timedOut) { throw "$name timed out" }
if ($exitCode -ne 0 -or $output -match '(?m)(ERROR|SCRIPT ERROR|FATAL):') { throw "$name failed, exit=$exitCode" }

$reports = [regex]::Matches($stdoutText, '(?m)^AI_SURVIVORS_SIMULATION_REPORT (\{.*\})\s*$')
if ($reports.Count -ne 1) { throw "${name}: expected exactly one simulation report, found $($reports.Count)" }
$report = $reports[0].Groups[1].Value | ConvertFrom-Json

$required = @('requested_seed', 'character_id', 'policy', 'normal_character', 'final_state',
    'survival_seconds', 'hp', 'maxhp', 'kills', 'level', 'xp', 'cards', 'invalid_selections',
    'peak_pending_levels', 'peak_enemies', 'peak_projectiles', 'peak_orbs', 'peak_weapons',
    'frames', 'trace_hash', 'error', 'iteration_limit', 'stalled', 'frozen_violations',
    'terminal_violations')
foreach ($field in $required) {
    if ($report.PSObject.Properties.Name -notcontains $field) { throw "${name}: report missing $field" }
}
if ($report.requested_seed -ne $Seed -or $report.character_id -ne $CharacterId -or $report.policy -ne $Policy) {
    throw "${name}: report does not match requested seed, character or policy"
}
if ($report.normal_character -ne $true) { throw "${name}: report is not from normal character data" }
if ($report.final_state -notin @('Won', 'Lost')) { throw "${name}: run did not reach a terminal state: $($report.final_state)" }
if (-not [string]::IsNullOrEmpty($report.error)) { throw "${name}: simulation error: $($report.error)" }
if ($report.trace_hash -notmatch '^[0-9a-f]{16}$') { throw "${name}: trace_hash is not 16 hex digits" }
if ($report.iteration_limit -or $report.stalled) { throw "${name}: run hit iteration limit or stalled" }
if ($report.invalid_selections -ne 0) { throw "${name}: invalid upgrade selection" }
if ($report.survival_seconds -lt 0 -or $report.survival_seconds -gt 1200 -or
    $report.hp -lt 0 -or $report.hp -gt $report.maxhp -or
    ($report.final_state -eq 'Won' -and ($report.survival_seconds -ne 1200 -or $report.hp -le 0)) -or
    ($report.final_state -eq 'Lost' -and ($report.survival_seconds -ge 1200 -or $report.hp -ne 0))) {
    throw "${name}: terminal metrics are inconsistent"
}
if ($report.frozen_violations -ne 0 -or $report.terminal_violations -ne 0) { throw "${name}: state invariant violated" }
if ($report.peak_enemies -gt 256 -or $report.peak_projectiles -gt 512 -or
    $report.peak_orbs -gt 512 -or $report.peak_weapons -gt 6) {
    throw "${name}: peak metrics exceed caps"
}
if ($RequireVictory -and $report.final_state -ne 'Won') {
    throw "${name}: -RequireVictory set but run ended $($report.final_state)"
}

Write-Host "Simulation report ($name):"
$report | ConvertTo-Json -Depth 4
