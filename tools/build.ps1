param([int]$Jobs = 8, [string]$Toolset = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $arguments = @('platform=windows', 'arch=x86_64', 'target=template_debug', '-j', "$Jobs")
    if ($Toolset) { $arguments += "msvc_toolset=$Toolset" }
    python -m SCons @arguments
    if ($LASTEXITCODE -ne 0) { throw "SCons failed: $LASTEXITCODE" }
} finally { Pop-Location }
