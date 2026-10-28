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
    # Start-Process joins ArgumentList on Windows; explicitly quote paths containing spaces.
    $quoted = @($Arguments | ForEach-Object { '"' + $_ + '"' })
    $launch = @{FilePath=$Godot; ArgumentList=$quoted; PassThru=$true; RedirectStandardOutput=$stdout; RedirectStandardError=$stderr}
    if ($IsWindows) { $launch.WindowStyle = 'Hidden' }
    $p = Start-Process @launch
    if (!$p.WaitForExit(60000)) { $p.Kill(); throw "$Name timed out" }
    $p.WaitForExit()
    $output = (Get-Content $stdout -Raw) + (Get-Content $stderr -Raw)
    Write-Host $output
    if ($p.ExitCode -ne 0 -or $output -match '(?m)(ERROR|SCRIPT ERROR|FATAL):') { throw "$Name failed, exit=$($p.ExitCode)" }
    if ($RequireMarker -and $output -notmatch '(?m)^AI_SURVIVORS_INTEGRATION_OK\s*$') { throw 'Native integration marker missing' }
}
Invoke-GodotCheck 'import' @('--headless', '--path', $root, '--editor', '--import', '--quit') $false
Invoke-GodotCheck 'integration' @('--headless', '--path', $root, '--', '--integration-test') $true
