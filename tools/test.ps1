param([string]$Godot = "$PSScriptRoot/../.tools/godot-4.7.2/Godot_v4.7.2-stable_win64_console.exe")
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$Godot = (Resolve-Path -LiteralPath $Godot).Path
$version = & $Godot --version
if ($LASTEXITCODE -ne 0 -or $version -ne '4.7.2.stable.official.ed1daf0bf') { throw "Unexpected Godot: $version" }
New-Item -ItemType Directory -Force "$root/build/test-results" | Out-Null
$testProgram = if ($IsWindows) { "$root/build/tests/ai_survivors_tests.exe" } else { "$root/build/tests/ai_survivors_tests" }
& $testProgram "--gtest_output=xml:$root/build/test-results/unit.xml"
if ($LASTEXITCODE -ne 0) { throw 'Unit tests failed' }

function Invoke-GodotCheck([string]$Name, [string[]]$Arguments, [bool]$RequireMarker) {
    $stdout = "$root/build/test-results/$Name.out.log"
    $stderr = "$root/build/test-results/$Name.err.log"
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Godot
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true
    foreach ($argument in $Arguments) { $startInfo.ArgumentList.Add($argument) }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw "$Name failed to start" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $timedOut = -not $process.WaitForExit(60000)
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
    if ($timedOut) { throw "$Name timed out" }
    if ($exitCode -ne 0 -or $output -match '(?m)(ERROR|SCRIPT ERROR|FATAL):') { throw "$Name failed, exit=$exitCode" }
    if ($RequireMarker -and $output -notmatch '(?m)^AI_SURVIVORS_INTEGRATION_OK\s*$') { throw 'Native integration marker missing' }
}
Invoke-GodotCheck 'import' @('--headless', '--path', $root, '--editor', '--import', '--quit') $false
Invoke-GodotCheck 'integration' @('--headless', '--path', $root, '--', '--integration-test') $true
