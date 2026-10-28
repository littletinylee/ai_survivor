$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    git submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw 'Submodule initialization failed' }
    python -m pip install -r tools/requirements.txt
    if ($LASTEXITCODE -ne 0) { throw 'SCons installation failed' }
    $dir = "$root/.tools"
    New-Item -ItemType Directory -Force $dir | Out-Null
    Set-Content "$dir/.gdignore" ''
    $asset = 'Godot_v4.7.2-stable_win64.exe.zip'
    $base = 'https://github.com/godotengine/godot/releases/download/4.7.2-stable'
    Invoke-WebRequest "$base/$asset" -OutFile "$dir/$asset"
    Invoke-WebRequest "$base/SHA512-SUMS.txt" -OutFile "$dir/SHA512-SUMS.txt"
    $entry = Get-Content "$dir/SHA512-SUMS.txt" | Where-Object { $_ -match "  $([regex]::Escape($asset))$" }
    if (!$entry -or (Get-FileHash "$dir/$asset" -Algorithm SHA512).Hash -ne ($entry -split '\s+')[0]) { throw 'Godot checksum mismatch' }
    Expand-Archive "$dir/$asset" "$dir/godot-4.7.2" -Force
} finally { Pop-Location }
