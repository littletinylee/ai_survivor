param(
    [string]$Godot = "$PSScriptRoot/../.tools/godot-4.7.2/Godot_v4.7.2-stable_win64_console.exe",
    [ValidateRange(1,3)][int]$ColdStartCount = 2
)
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
# --frame-delay 1000 works around a Godot editor shutdown race (godotengine/godot#111048): deferred
# EditorHelp::_gen_extensions_docs can run after doc cleanup when --import quits immediately.
# It only delays shutdown; any non-zero exit or error output still fails the check. See docs/COLD_START.md.
Invoke-GodotCheck 'import' @('--headless', '--path', $root, '--editor', '--import', '--frame-delay', '1000', '--quit') $false
Invoke-GodotCheck 'integration' @('--headless', '--path', $root, '--', '--integration-test') $true

# Cold-start regression: each fixture is a fresh project without a .godot cache.
$extensionLibrary = 'ai_survivors.windows.template_debug.x86_64.dll'
$libraryPath = "$root/bin/$extensionLibrary"
if (-not (Test-Path -LiteralPath $libraryPath -PathType Leaf)) { throw "Extension library missing: $libraryPath" }
for ($i = 1; $i -le $ColdStartCount; $i++) {
    $fixture = "$root/build/cold-start-$([guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Path "$fixture/bin" | Out-Null
    foreach ($file in @('project.godot', 'ai_survivors.gdextension')) {
        Copy-Item -LiteralPath "$root/$file" -Destination "$fixture/$file"
    }
    foreach ($directory in @('scenes', 'data', 'assets')) {
        if (Test-Path -LiteralPath "$root/$directory" -PathType Container) {
            Copy-Item -LiteralPath "$root/$directory" -Destination "$fixture/$directory" -Recurse
        } elseif ($directory -ne 'assets') {
            throw "Required directory missing: $root/$directory"
        }
    }
    Copy-Item -LiteralPath $libraryPath -Destination "$fixture/bin/$extensionLibrary"
    Invoke-GodotCheck "cold-import-$i" @('--headless', '--path', $fixture, '--editor', '--import', '--frame-delay', '1000', '--quit') $false
    Invoke-GodotCheck "cold-game-$i" @('--headless', '--path', $fixture, '--', '--integration-test') $true
}
Write-Host "Cold-start fixtures passed: $ColdStartCount"
