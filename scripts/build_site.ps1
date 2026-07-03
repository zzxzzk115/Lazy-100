# Build the Lazy-100 wasm console and assemble the static site into an output dir.
# Windows/local convenience mirror of scripts/build_site.sh (CI uses the .sh on Linux).
#
#   scripts/build_site.ps1 [-Out <dir>]
#
# Then serve it with any static server, e.g.:
#   python -m http.server -d build/site 8000   ->  http://localhost:8000
[CmdletBinding()]
param([string]$Out)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $Out) { $Out = Join-Path $Root "build/site" }
$WasmDir = Join-Path $Root "build/wasm/wasm32/release"
if (-not $env:XMAKE_ROOT) { $env:XMAKE_ROOT = "y" }

Write-Host "==> building wasm console"
Push-Location $Root
try {
    & xmake f -p wasm -y
    if ($LASTEXITCODE -ne 0) { throw "xmake config (wasm) failed" }
    & xmake build -y lazy100
    if ($LASTEXITCODE -ne 0) { throw "xmake build (lazy100) failed" }
} finally { Pop-Location }

Write-Host "==> assembling site -> $Out"
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Copy-Item (Join-Path $Root "web/site/*") $Out -Recurse -Force
Copy-Item (Join-Path $WasmDir "lazy100.js")   $Out -Force
Copy-Item (Join-Path $WasmDir "lazy100.wasm") $Out -Force

Write-Host "==> done. site at: $Out"
Write-Host "    serve with:  python -m http.server -d `"$Out`" 8000   ->  http://localhost:8000"
