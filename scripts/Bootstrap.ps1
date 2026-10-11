param([string]$ToolsDirectory = (Join-Path $PSScriptRoot '..\.tools'))
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$manifest = Get-Content -LiteralPath (Join-Path $projectRoot 'vcpkg.json') -Raw | ConvertFrom-Json
$baseline = $manifest.'builtin-baseline'
if ($baseline -notmatch '^[a-f0-9]{40}$') { throw 'Invalid vcpkg baseline.' }
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'Install Git first.' }
$vcpkgDirectory = Join-Path $ToolsDirectory 'vcpkg'
if (-not (Test-Path -LiteralPath $vcpkgDirectory)) {
    New-Item -ItemType Directory -Path $ToolsDirectory -Force | Out-Null
    & git clone https://github.com/microsoft/vcpkg.git $vcpkgDirectory
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg clone failed.' }
    & git -C $vcpkgDirectory checkout --detach $baseline
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg baseline checkout failed.' }
}
$head = & git -C $vcpkgDirectory rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head.Trim() -ne $baseline) {
    throw 'The existing vcpkg checkout does not match the project baseline. Use a separate tools directory.'
}
& (Join-Path $vcpkgDirectory 'bootstrap-vcpkg.bat') -disableMetrics
if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed.' }
$env:VCPKG_ROOT = (Resolve-Path -LiteralPath $vcpkgDirectory).Path
Write-Host "vcpkg ready at $env:VCPKG_ROOT"
Write-Host 'VCPKG_ROOT is configured in this PowerShell session.'
