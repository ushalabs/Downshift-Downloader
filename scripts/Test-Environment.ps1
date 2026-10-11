$ErrorActionPreference = 'Stop'
$missing = [System.Collections.Generic.List[string]]::new()
foreach ($tool in @('git', 'cmake')) {
    $command = Get-Command $tool -ErrorAction SilentlyContinue
    if ($command) { Write-Host "$tool : $($command.Source)" }
    else { $missing.Add($tool) }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    $installation = & $vswhere -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) { $missing.Add('Visual Studio 2022 C++ Build Tools') }
    else { Write-Host "MSVC : $installation" }
} else { $missing.Add('Visual Studio 2022 C++ Build Tools') }
if ($env:VCPKG_ROOT -and (Test-Path -LiteralPath (Join-Path $env:VCPKG_ROOT 'vcpkg.exe'))) {
    Write-Host "vcpkg : $env:VCPKG_ROOT"
} else { $missing.Add('vcpkg / VCPKG_ROOT (run scripts/Bootstrap.ps1)') }
if (-not [Environment]::Is64BitOperatingSystem) { $missing.Add('64-bit Windows') }
if ($missing.Count -gt 0) {
    Write-Host 'Build prerequisites missing:'
    $missing | ForEach-Object { Write-Host " - $_" }
    exit 1
}
Write-Host 'Build prerequisites found. Configure to validate compiler and dependency versions.'
