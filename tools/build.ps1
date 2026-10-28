param([int]$Jobs = 8)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    python -m SCons platform=windows arch=x86_64 target=template_debug -j $Jobs
    if ($LASTEXITCODE -ne 0) { throw "SCons failed: $LASTEXITCODE" }
} finally { Pop-Location }
