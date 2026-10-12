param([string]$ToolsDirectory = (Join-Path $PSScriptRoot '..\.tools\windows'))
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$manifest = Get-Content -LiteralPath (Join-Path $projectRoot 'vcpkg.json') -Raw | ConvertFrom-Json
$baseline = $manifest.'builtin-baseline'
if ($baseline -notmatch '^[a-f0-9]{40}$') { throw 'Invalid vcpkg baseline.' }
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'Install Git first.' }
# Visual Studio bundles CMake without necessarily adding it to the user's PATH.
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -eq 0 -and $installation) {
            $cmakeBin = Join-Path $installation.Trim() 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
            if (Test-Path -LiteralPath (Join-Path $cmakeBin 'cmake.exe')) {
                $env:PATH = "$cmakeBin;$env:PATH"
            }
        }
    }
}
$vcpkgDirectory = Join-Path $ToolsDirectory 'vcpkg'
if (-not (Test-Path -LiteralPath $vcpkgDirectory)) {
    New-Item -ItemType Directory -Path $ToolsDirectory -Force | Out-Null
    & git clone https://github.com/microsoft/vcpkg.git $vcpkgDirectory
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg clone failed.' }
    & git -C $vcpkgDirectory checkout --detach $baseline
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg baseline checkout failed.' }
}
$head = & git -C $vcpkgDirectory rev-parse --verify HEAD 2>$null
if ($LASTEXITCODE -ne 0) {
    # Recover our interrupted clone without resetting a completed checkout.
    $originUrl = & git -C $vcpkgDirectory remote get-url origin
    if ($LASTEXITCODE -ne 0 -or $originUrl.Trim() -ne 'https://github.com/microsoft/vcpkg.git') {
        throw 'The incomplete tools directory is not the expected vcpkg repository. Use a separate tools directory.'
    }
    & git -C $vcpkgDirectory fetch --depth=1 origin $baseline
    if ($LASTEXITCODE -ne 0) { throw 'Unable to finish the pinned vcpkg source download.' }
    & git -C $vcpkgDirectory checkout --detach $baseline
    if ($LASTEXITCODE -ne 0) { throw 'Unable to finish the vcpkg baseline checkout.' }
    $head = & git -C $vcpkgDirectory rev-parse --verify HEAD
}
if ($LASTEXITCODE -ne 0 -or $head.Trim() -ne $baseline) {
    throw 'The existing vcpkg checkout does not match the project baseline. Use a separate tools directory.'
}
$vcpkgExecutable = Join-Path $vcpkgDirectory 'vcpkg.exe'
if (-not (Test-Path -LiteralPath $vcpkgExecutable)) {
    & (Join-Path $vcpkgDirectory 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed.' }
}
$env:VCPKG_DISABLE_METRICS = '1'
& $vcpkgExecutable version
if ($LASTEXITCODE -ne 0) { throw 'The vcpkg executable could not start.' }
$env:VCPKG_ROOT = (Resolve-Path -LiteralPath $vcpkgDirectory).Path
Write-Host "vcpkg ready at $env:VCPKG_ROOT"
Write-Host 'VCPKG_ROOT is configured in this PowerShell session.'
