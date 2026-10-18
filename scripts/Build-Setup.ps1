param([string]$OutputDirectory='', [string]$NsisCompiler='', [switch]$RequireClean, [switch]$LocalPreview, [string]$BinaryDirectory='', [string]$VerificationSetupPath='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'dist'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(!$NsisCompiler){$NsisCompiler=Join-Path $root '.tools/windows/nsis/nsis-3.12/makensis.exe'}
if(!(Test-Path -LiteralPath $NsisCompiler -PathType Leaf)){throw 'Pass -NsisCompiler with the path to the portable NSIS 3 compiler.'}
& (Join-Path $PSScriptRoot 'Build-Portable.ps1') -OutputDirectory $OutputDirectory -RequireClean:$RequireClean -LocalPreview:$LocalPreview -BinaryDirectory $BinaryDirectory
Copy-Item -LiteralPath (Join-Path $root 'packaging/Read-me.txt') -Destination (Join-Path $OutputDirectory 'READ-ME.txt') -Force
$instructions=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'chrome-setup-template.txt') -Raw -Encoding utf8
$instructions.Replace('{{EXTENSION_PATH}}','%ProgramFiles%\Downshift\extension') | Set-Content -LiteralPath (Join-Path $OutputDirectory 'Chrome-setup.txt') -Encoding utf8
$version=[regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw),'project\(Downshift VERSION ([0-9.]+)').Groups[1].Value
$stage=Join-Path $OutputDirectory ('setup-stage-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $OutputDirectory "Downshift-$version-windows-x64.zip"),$stage)
$payload=Join-Path $stage "Downshift-$version-windows-x64"
$install=Join-Path $stage 'install-files.nsh';$uninstall=Join-Path $stage 'uninstall-files.nsh'
function Quote-Nsis([string]$value){return $value.Replace('$','$$').Replace('"','$\"')}
$installLines=[Collections.Generic.List[string]]::new();$uninstallLines=[Collections.Generic.List[string]]::new()
$files=@(Get-ChildItem -LiteralPath $payload -File -Recurse)
foreach($file in $files){
    $relative=$file.FullName.Substring($payload.Length+1)
    $directory=Split-Path $relative -Parent
    $installLines.Add('SetOutPath "$INSTDIR'+$(if($directory){'\'+(Quote-Nsis $directory)}else{''})+'"')
    $installLines.Add('File "'+(Quote-Nsis $file.FullName)+'"')
    $uninstallLines.Add('Delete "$INSTDIR\'+(Quote-Nsis $relative)+'"')
}
foreach($directory in (Get-ChildItem -LiteralPath $payload -Directory -Recurse | Sort-Object {$_.FullName.Length} -Descending)){
    $uninstallLines.Add('RMDir "$INSTDIR\'+(Quote-Nsis $directory.FullName.Substring($payload.Length+1))+'"')
}
[IO.File]::WriteAllLines($install,$installLines,[Text.UTF8Encoding]::new($true))
[IO.File]::WriteAllLines($uninstall,$uninstallLines,[Text.UTF8Encoding]::new($true))
$setup=Join-Path $OutputDirectory "Downshift-$version-Setup-x64.exe"
& $NsisCompiler /V2 "/DVERSION=$version" "/DPROJECT_ROOT=$root" "/DSETUP_OUTPUT=$setup" "/DINSTALL_FILES=$install" "/DUNINSTALL_FILES=$uninstall" (Join-Path $root 'packaging/Downshift.nsi')
if($LASTEXITCODE -ne 0){throw 'NSIS compilation failed.'}
if($VerificationSetupPath){
    $VerificationSetupPath=[IO.Path]::GetFullPath($VerificationSetupPath)
    New-Item -ItemType Directory -Path (Split-Path $VerificationSetupPath -Parent) -Force | Out-Null
    & $NsisCompiler /V2 /DISOLATED_TEST_BUILD "/DVERSION=$version" "/DPROJECT_ROOT=$root" "/DSETUP_OUTPUT=$VerificationSetupPath" "/DINSTALL_FILES=$install" "/DUNINSTALL_FILES=$uninstall" (Join-Path $root 'packaging/Downshift.nsi')
    if($LASTEXITCODE -ne 0){throw 'Private NSIS verification build failed.'}
}
$manifest=Get-Content -LiteralPath (Join-Path $payload 'package-manifest.json') -Raw | ConvertFrom-Json
$bytes=($files|Measure-Object Length -Sum).Sum
@{version=$version;compression='NSIS solid LZMA, 32 MiB dictionary';payloadBytes=$bytes;setupBytes=(Get-Item -LiteralPath $setup).Length;setupSha256=(Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLower();compilerSha256=(Get-FileHash -LiteralPath $NsisCompiler -Algorithm SHA256).Hash.ToLower();localPreview=$manifest.localPreview;sourceDirty=$manifest.sourceDirty;signed=$false;architecture='x64';requiresAdministrator=$true;defaultInstallDirectory='ProgramFiles64/Downshift';desktopShortcut=$true;installationScope='all users';nativeHostRegistryHive='HKLM (64-bit)';videoTools='automatically downloaded from pinned upstream releases during setup';internetRequiredForVideoSetup=$true;chromeIntegration='native host registration and guided Load unpacked; no Chrome Web Store listing yet'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputDirectory 'setup-manifest.json') -Encoding utf8
Get-ChildItem -LiteralPath $OutputDirectory -File | Where-Object {$_.Name -match '^Downshift-.*\.(zip|exe)$'} | Sort-Object Name | ForEach-Object {"$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower())  $($_.Name)"}|Set-Content -LiteralPath (Join-Path $OutputDirectory 'SHA256SUMS.txt') -Encoding ascii
$resolved=[IO.Path]::GetFullPath($stage)
if(!$resolved.StartsWith($OutputDirectory+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Split-Path $resolved -Leaf) -notlike 'setup-stage-*'){throw 'Unsafe staging cleanup path.'}
Remove-Item -LiteralPath $resolved -Recurse -Force
Write-Host "Compressed Windows setup created: $setup"
