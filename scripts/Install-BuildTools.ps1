# Run this script yourself from an ordinary PowerShell window. Windows may ask
# for administrator approval. It never commits or pushes project changes.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$toolsRoot = Join-Path $projectRoot '.tools'
New-Item -ItemType Directory -Path $toolsRoot -Force | Out-Null
$installerPath = Join-Path $toolsRoot 'vs_buildtools.exe'
if (-not (Test-Path -LiteralPath $installerPath)) {
    Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vs_buildtools.exe' -OutFile $installerPath
}
$signature = Get-AuthenticodeSignature -LiteralPath $installerPath
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
    throw 'The installer must have a valid Microsoft Authenticode signature.'
}
$arguments = '--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
Write-Host 'Windows will ask for administrator approval to install the C++ compiler and Windows SDK.'
$installerProcess = Start-Process -FilePath $installerPath -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
if ($installerProcess.ExitCode -notin @(0, 3010)) {
    throw "Build Tools installation returned exit code $($installerProcess.ExitCode)."
}
if ($installerProcess.ExitCode -eq 3010) { Write-Host 'Installation succeeded; Windows restart is required before building.' }
else { Write-Host 'Installation finished. Reopen PowerShell before checking the environment.' }
Write-Host 'Ensure CMake is installed and available on PATH; select the C++ CMake tools component in the Visual Studio installer if needed.'
