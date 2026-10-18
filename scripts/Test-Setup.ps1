param([Parameter(Mandatory=$true)][string]$SetupPath,[Parameter(Mandatory=$true)][string]$PackagePath,[string]$ReportPath='', [string]$VideoTestDriver='', [string]$PythonExecutable='', [string]$MediaArchiveCache='', [string]$VerificationSetupPath='', [switch]$InstallerOnly, [string]$PreviousMediaReport='', [string]$PreviousPortableReport='', [switch]$ElevatedProduction, [string]$YouTubeTestDriver='', [string]$YouTubeReportPath='')
$ErrorActionPreference='Stop'
$setup=(Resolve-Path -LiteralPath $SetupPath).Path
$testSetup=if($ElevatedProduction){$setup}elseif($VerificationSetupPath){(Resolve-Path -LiteralPath $VerificationSetupPath).Path}else{$setup}
$temporary=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
if($ElevatedProduction){
    $principal=[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    if(!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Production installation acceptance requires administrator approval.'}
    $temporary=[IO.Path]::GetFullPath($(if($env:ProgramW6432){$env:ProgramW6432}else{$env:ProgramFiles}))
}
$testRoot=Join-Path $temporary ('Downshift setup test '+[guid]::NewGuid().ToString('N'))
$install=Join-Path $testRoot 'Installed app'
$registry=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
$normalHost='Software\Google\Chrome\NativeMessagingHosts\org.ushalabs.downshift'
$testHost='Software\Google\Chrome\NativeMessagingHosts\org.ushalabs.downshift.packaging-test'
$aliasHost=$testHost+'.user-override'
$testUninstall='Software\Microsoft\Windows\CurrentVersion\Uninstall\Downshift.PackagingTest'
function Read-Default([string]$path){$key=$registry.OpenSubKey($path);try{if($key){return $key.GetValue('')}}finally{if($key){$key.Dispose()}};return $null}
function Run-Hidden([string]$file,[string]$arguments){
    $process=Start-Process -FilePath $file -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(1860000)){throw 'Private setup process timed out.'}
    if($process.ExitCode -ne 0){throw "Setup process failed: $($process.ExitCode)"}
}
$previousNormal=Read-Default $normalHost;$previousTest=Read-Default $testHost
$existing=$registry.OpenSubKey($testUninstall);if($existing){$existing.Dispose();$registry.Dispose();throw 'A previous isolated setup is registered; refusing to overwrite it.'}
$desktop=Join-Path ([Environment]::GetFolderPath('Desktop')) 'Downshift.lnk'
$desktopBefore=if(Test-Path -LiteralPath $desktop){(Get-FileHash -LiteralPath $desktop -Algorithm SHA256).Hash}else{$null}
$pathBefore=$env:PATH;$pluginBefore=$env:QT_PLUGIN_PATH;$platformBefore=$env:QT_QPA_PLATFORM
$checks=0;$succeeded=$false
$previousAlias=Read-Default $aliasHost
try{
    $versionInfo=[Diagnostics.FileVersionInfo]::GetVersionInfo($setup)
    if($versionInfo.CompanyName -ne 'UshaLabs'){throw 'Setup developer attribution differs.'};$checks++
    if(!$VerificationSetupPath -and !$ElevatedProduction){throw 'Use a private -VerificationSetupPath or administrator-approved -ElevatedProduction acceptance.'}
    if(!('DownshiftSetupManifest' -as [type])){Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class DownshiftSetupManifest {
 [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr LoadLibraryEx(string file, IntPtr reserved, uint flags);
 [DllImport("kernel32", SetLastError=true)] static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
 [DllImport("kernel32")] static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
 [DllImport("kernel32")] static extern IntPtr LockResource(IntPtr resource);
 [DllImport("kernel32")] static extern uint SizeofResource(IntPtr module, IntPtr resource);
 [DllImport("kernel32")] static extern bool FreeLibrary(IntPtr module);
 public static string Read(string file) {
  var module=LoadLibraryEx(file, IntPtr.Zero, 2); if(module==IntPtr.Zero) throw new Exception("Cannot read setup resources.");
  try {var resource=FindResource(module,new IntPtr(1),new IntPtr(24)); if(resource==IntPtr.Zero) throw new Exception("Setup manifest missing.");
   var bytes=new byte[SizeofResource(module,resource)]; Marshal.Copy(LockResource(LoadResource(module,resource)),bytes,0,bytes.Length); return System.Text.Encoding.UTF8.GetString(bytes);
  } finally {FreeLibrary(module);}
 }
}
'@}
    if([DownshiftSetupManifest]::Read($setup) -notmatch 'level="requireAdministrator"' -or (!$ElevatedProduction -and [DownshiftSetupManifest]::Read($testSetup) -notmatch 'level="asInvoker"')){throw 'Production/private setup elevation manifests differ from their declared scopes.'};$checks++
    New-Item -ItemType Directory -Path $testRoot | Out-Null
    $legacyRoot=Join-Path $testRoot 'Old portable host';New-Item -ItemType Directory -Path $legacyRoot|Out-Null
    $legacyHost=Join-Path $legacyRoot 'DownshiftNativeHost.exe';[IO.File]::WriteAllBytes($legacyHost,[byte[]](77,90))
    $legacyManifest=Join-Path $legacyRoot 'org.ushalabs.downshift.packaging-test.json'
    $legacyDesktop=Join-Path $legacyRoot 'Downshift.lnk';$legacyShell=New-Object -ComObject WScript.Shell;$legacyLink=$legacyShell.CreateShortcut($legacyDesktop);$legacyLink.TargetPath=Join-Path $legacyRoot 'Downshift.exe';$legacyLink.Save();$legacyDesktopHash=(Get-FileHash -LiteralPath $legacyDesktop).Hash
    @{name='org.ushalabs.downshift.packaging-test';type='stdio';path=$legacyHost;allowed_origins=@('chrome-extension://empnpdkfnffpcgnfgdndkbielplcoioe/')}|ConvertTo-Json|Set-Content -LiteralPath $legacyManifest -Encoding utf8
    $aliasKey=$registry.CreateSubKey($aliasHost);try{$aliasKey.SetValue('',$legacyManifest)}finally{$aliasKey.Dispose()}
    if($MediaArchiveCache){
        # Verification-only cache seeding avoids re-downloading immutable archives
        # while still exercising the installer's actual extraction/hash path.
        $archiveLock=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'media-tools-lock.json') -Raw | ConvertFrom-Json
        $cache=Join-Path $install '.setup-media-cache';New-Item -ItemType Directory -Path $cache -Force | Out-Null
        foreach($asset in $archiveLock.assets){
            $source=Join-Path $MediaArchiveCache $asset.name
            if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLower() -ne $asset.sha256){throw 'Fixture archive checksum differs.'}
            Copy-Item -LiteralPath $source -Destination $cache
        }
    }
    Run-Hidden $testSetup "/S /ISOLATEDTEST /D=$install"
    if((Read-Default $aliasHost) -ne (Join-Path $install 'org.ushalabs.downshift.packaging-test.json')){throw 'The older per-user host still overrides the installed host.'};$checks++
    if($legacyShell.CreateShortcut($legacyDesktop).TargetPath -ne (Join-Path $install 'Downshift.exe')){throw 'The older user Desktop icon still opens the development app.'};$checks++
    $manifest=Get-Content -LiteralPath (Join-Path $install 'package-manifest.json') -Raw | ConvertFrom-Json
    foreach($file in $manifest.files){if($file.path -eq 'Chrome-setup.txt'){continue};if((Get-FileHash -LiteralPath (Join-Path $install $file.path) -Algorithm SHA256).Hash.ToLower() -ne $file.sha256){throw "Installed file differs: $($file.path)"}};$checks++
    $mediaLock=Get-Content -LiteralPath (Join-Path $install 'scripts/media-tools-lock.json') -Raw|ConvertFrom-Json
    foreach($file in $mediaLock.files){if((Get-FileHash -LiteralPath (Join-Path $install ('media-tools/'+$file.name)) -Algorithm SHA256).Hash.ToLower() -ne $file.sha256){throw "Automatic video install differs: $($file.name)"}};$checks++
    if(Test-Path -LiteralPath (Join-Path $install '.setup-media-cache')){throw 'Successful setup retained video download cache.'};$checks++
    $guide=Get-Content -LiteralPath (Join-Path $install 'Chrome-setup.txt') -Raw
    $expectedGuide=(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'chrome-setup-template.txt') -Raw -Encoding utf8).Replace('{{EXTENSION_PATH}}',(Join-Path $install 'extension'))
    if($guide -cne $expectedGuide -or $guide.Contains('{{EXTENSION_PATH}}')){throw 'Chrome text instructions differ.'};$checks++
    if(!(Test-Path -LiteralPath (Join-Path $install 'Uninstall.exe'))){throw 'Uninstaller missing.'};$checks++
    $key=$registry.OpenSubKey($testUninstall);try{if(!$key -or $key.GetValue('InstallLocation') -ne $install -or $key.GetValue('DisplayVersion') -ne $manifest.version -or $key.GetValue('Publisher') -ne 'UshaLabs'){throw 'Windows uninstall registration differs.'}}finally{if($key){$key.Dispose()}};$checks++
    if((Read-Default $testHost) -ne (Join-Path $install 'org.ushalabs.downshift.packaging-test.json')){throw 'Isolated Chrome registration missing.'};$checks++
    $shell=New-Object -ComObject WScript.Shell
    if($shell.CreateShortcut((Join-Path $install 'Downshift-test.lnk')).TargetPath -ne (Join-Path $install 'Downshift.exe')){throw 'Installed shortcut points elsewhere.'};$checks++
    $desktopFixture=Join-Path $install 'Desktop/Downshift.lnk'
    $desktopLink=$shell.CreateShortcut($desktopFixture)
    if(!(Test-Path -LiteralPath $desktopFixture) -or $desktopLink.TargetPath -ne (Join-Path $install 'Downshift.exe') -or $desktopLink.WorkingDirectory -ne $install -or $desktopLink.IconLocation -ne ((Join-Path $install 'Downshift.exe')+',0')){throw 'Installed Desktop shortcut target, working directory or icon differs.'};$checks++
    $guidePath=Join-Path $install 'Chrome-setup.txt';$guideTime=(Get-Item -LiteralPath $guidePath).LastWriteTimeUtc
    $instructionSignal=[pscustomobject]@{opened=$false}
    function Invoke-Item {param([string]$LiteralPath) if($LiteralPath -ne $guidePath){throw 'Instructions opened another document.'};$instructionSignal.opened=$true}
    try{& (Join-Path $install 'scripts/Open-ChromeSetup.ps1')}finally{Remove-Item Function:/Invoke-Item}
    if(!$instructionSignal.opened -or (Get-Item -LiteralPath $guidePath).LastWriteTimeUtc -ne $guideTime){throw 'Opening installed instructions attempted to rewrite the protected app directory.'};$checks++
    # Unknown user data must survive both upgrades and uninstall, even in the app directory.
    $sentinel=Join-Path $install 'user-download.keep';[IO.File]::WriteAllText($sentinel,'Downloaded content must survive uninstall.')
    $data=Join-Path $install '.downshift-data';New-Item -ItemType Directory -Path $data | Out-Null
    $history=Join-Path $data 'history.keep';[IO.File]::WriteAllText($history,'Private history preservation fixture.')
    $sentinelHash=(Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash
    $historyHash=(Get-FileHash -LiteralPath $history -Algorithm SHA256).Hash
    $mediaUnknown=Join-Path $install 'media-tools/user-video.keep';[IO.File]::WriteAllText($mediaUnknown,'Unknown content must be preserved.')
    Run-Hidden $testSetup "/S /ISOLATEDTEST /D=$install"
    if((Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash -ne $sentinelHash -or (Get-FileHash -LiteralPath $history -Algorithm SHA256).Hash -ne $historyHash){throw 'Reinstall changed private user data.'};$checks++
    $env:PATH="$env:WINDIR\System32;$env:WINDIR";$env:QT_PLUGIN_PATH='';$env:QT_QPA_PLATFORM='windows'
    Run-Hidden (Join-Path $install 'Downshift.exe') '--smoke-test';$checks++
    $previousVideoChecks=0
    $applicationSha=(Get-FileHash -LiteralPath (Join-Path $install 'Downshift.exe') -Algorithm SHA256).Hash.ToLower()
    if($InstallerOnly){
        if(!$PreviousMediaReport -or !$PreviousPortableReport){throw 'Installer-only checks require the previous verified media and application reports.'}
        $priorMedia=Get-Content -LiteralPath $PreviousMediaReport -Raw | ConvertFrom-Json
        $priorPortable=Get-Content -LiteralPath $PreviousPortableReport -Raw | ConvertFrom-Json
        $applicationSha=(Get-FileHash -LiteralPath (Join-Path $install 'Downshift.exe') -Algorithm SHA256).Hash.ToLower()
        if(!$priorMedia.automaticVideoInstallVerified -or $priorMedia.videoGuiChecks -lt 130 -or !$priorPortable.sourceSnapshotVerified -or $priorPortable.nativeApplicationSha256 -ne $applicationSha){throw 'Previously tested application differs; run full media acceptance.'}
        $previousVideoChecks=[int]$priorMedia.videoGuiChecks;$videoChecks=0
    }else{
    if(!$VideoTestDriver -or !$PythonExecutable){throw 'Installer video acceptance requires -VideoTestDriver and -PythonExecutable.'}
    $driver=Join-Path $install 'downshift_media_tests.exe'
    Copy-Item -LiteralPath $VideoTestDriver -Destination $driver
    $mediaOutput=Join-Path $testRoot 'media-output.txt';$mediaError=Join-Path $testRoot 'media-error.txt'
    $fixture=Join-Path (Split-Path $PSScriptRoot -Parent) 'tests/MediaIntegration.py'
    $process=Start-Process -FilePath $PythonExecutable -ArgumentList ('"'+$fixture+'" "'+$driver+'" "'+(Join-Path $testRoot 'media-shots')+'"') -WindowStyle Hidden -RedirectStandardOutput $mediaOutput -RedirectStandardError $mediaError -PassThru
    if(!$process.WaitForExit(480000)){throw 'Installed video fixture timed out.'}
    $videoLog=Get-Content -LiteralPath $mediaOutput -Raw
    if($process.ExitCode -ne 0){throw "Installed video acceptance failed: $videoLog $(Get-Content -LiteralPath $mediaError -Raw)"}
    Write-Host $videoLog
    $videoChecks=[regex]::Match($videoLog,'Media GUI checks passed: (\d+)').Groups[1].Value
    if(!$videoChecks){throw 'Installed video fixture returned no check count.'};$checks++
    }
    $publicYouTubeVerified=$false
    if($YouTubeTestDriver){
        if(!$PythonExecutable -or !$YouTubeReportPath){throw 'Public YouTube acceptance requires a Python executable and report path.'}
        $liveDriver=Join-Path $install 'downshift_youtube_live_tests.exe';Copy-Item -LiteralPath $YouTubeTestDriver -Destination $liveDriver
        $liveScript=Join-Path (Split-Path $PSScriptRoot -Parent) 'tests/YouTubeInstalledAcceptance.py'
        $liveProcess=Start-Process -FilePath $PythonExecutable -ArgumentList ('"'+$liveScript+'" "'+$liveDriver+'" "'+$YouTubeReportPath+'"') -WindowStyle Hidden -RedirectStandardOutput (Join-Path $testRoot 'youtube-output.txt') -RedirectStandardError (Join-Path $testRoot 'youtube-error.txt') -PassThru
        if(!$liveProcess.WaitForExit(1800000)){throw 'Installed public YouTube acceptance timed out.'}
        if($liveProcess.ExitCode -ne 0){throw "Installed public YouTube failed: $(Get-Content (Join-Path $testRoot 'youtube-error.txt') -Raw)"}
        $liveReport=Get-Content -LiteralPath $YouTubeReportPath -Raw|ConvertFrom-Json
        if(@($liveReport.publicTransfers).Count -ne 2 -or @($liveReport.publicTransfers|Where-Object {!$_.completed -or !$_.audioDecoded}).Count){throw 'Installed public YouTube report is incomplete.'}
        $publicYouTubeVerified=$true;$checks++
    }
    # Launch the installed Deno with no developer runtime on PATH, too.
    Run-Hidden (Join-Path $install 'media-tools/deno.exe') '--version';$checks++
    $env:PATH=$pathBefore;$env:QT_PLUGIN_PATH=$pluginBefore;$env:QT_QPA_PLATFORM=$platformBefore
    # Verify a failed bootstrap cannot overwrite an existing file or claim success.
    $failure=Join-Path $testRoot 'failed-bootstrap';$failureScripts=Join-Path $failure 'scripts'
    New-Item -ItemType Directory -Path $failureScripts | Out-Null
    Copy-Item -LiteralPath (Join-Path $install 'scripts/Install-MediaTools.ps1') -Destination $failureScripts
    $failureLock=Get-Content -LiteralPath (Join-Path $install 'scripts/media-tools-lock.json') -Raw|ConvertFrom-Json
    foreach($asset in $failureLock.assets){$asset.url='https://127.0.0.1:9/no-upstream'}
    $failureLock|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $failureScripts 'media-tools-lock.json') -Encoding utf8
    $failureDestination=Join-Path $failure 'media-tools';New-Item -ItemType Directory -Path $failureDestination | Out-Null
    $failureFile=Join-Path $failureDestination 'yt-dlp.exe';[IO.File]::WriteAllText($failureFile,'Existing helper must survive failed downloads.')
    $failureHash=(Get-FileHash -LiteralPath $failureFile -Algorithm SHA256).Hash
    $failureProcess=Start-Process -FilePath (Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe') -ArgumentList ('-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+(Join-Path $failureScripts 'Install-MediaTools.ps1')+'" -Destination "'+$failureDestination+'" -CacheDirectory "'+(Join-Path $failure 'cache')+'"') -WindowStyle Hidden -RedirectStandardOutput (Join-Path $failure 'output.txt') -RedirectStandardError (Join-Path $failure 'error.txt') -PassThru
    if(!$failureProcess.WaitForExit(30000) -or $failureProcess.ExitCode -eq 0){throw 'Failed upstream download did not report failure.'}
    if((Get-FileHash -LiteralPath $failureFile -Algorithm SHA256).Hash -ne $failureHash -or (Test-Path -LiteralPath (Join-Path $failureDestination 'installed-files.json'))){throw 'Failed bootstrap replaced helper or created success receipt.'};$checks++
    $modifiedLicense=Join-Path $install 'media-tools/FFMPEG-LICENSE.txt';[IO.File]::WriteAllText($modifiedLicense,'Modified file must be preserved by uninstall.')
    $modifiedHash=(Get-FileHash -LiteralPath $modifiedLicense -Algorithm SHA256).Hash
    Run-Hidden (Join-Path $install 'Uninstall.exe') "/S _?=$install"
    foreach($file in $mediaLock.files){if($file.name -ne 'FFMPEG-LICENSE.txt' -and (Test-Path -LiteralPath (Join-Path $install ('media-tools/'+$file.name)))){throw 'Uninstall retained an unchanged owned video runtime.'}};$checks++
    if((Get-FileHash -LiteralPath $modifiedLicense -Algorithm SHA256).Hash -ne $modifiedHash){throw 'Uninstall changed a modified video file.'};$checks++
    if(!(Test-Path -LiteralPath $mediaUnknown)){throw 'Video uninstall removed unknown content.'};$checks++
    if(Test-Path -LiteralPath (Join-Path $install 'Downshift.exe')){throw 'Uninstall left the application executable.'};$checks++
    if(Test-Path -LiteralPath $desktopFixture){throw 'Uninstall retained the owned Desktop shortcut.'};$checks++
    if((Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash -ne $sentinelHash -or (Get-FileHash -LiteralPath $history -Algorithm SHA256).Hash -ne $historyHash){throw 'Uninstall changed private user data.'};$checks++
    if((Read-Default $testHost) -ne $previousTest){throw 'Test host registration was not restored.'};$checks++
    if((Read-Default $aliasHost) -ne $legacyManifest){throw 'Uninstall did not restore the older owned per-user host.'};$checks++
    if((Get-FileHash -LiteralPath $legacyDesktop).Hash -ne $legacyDesktopHash){throw 'Uninstall did not restore the older owned Desktop shortcut.'};$checks++
    $key=$registry.OpenSubKey($testUninstall);if($key){$key.Dispose();throw 'Uninstall registration was retained.'};$checks++
    if((Read-Default $normalHost) -ne $previousNormal){throw 'Normal Chrome registration changed.'};$checks++
    $desktopAfter=if(Test-Path -LiteralPath $desktop){(Get-FileHash -LiteralPath $desktop -Algorithm SHA256).Hash}else{$null}
    if($desktopAfter -ne $desktopBefore){throw 'Normal Desktop shortcut changed.'};$checks++
    $report=@{checks=$checks;automaticVideoInstallVerified=$true;freshUpstreamDownloadsVerified=(!$MediaArchiveCache);verifiedArchiveCacheSeeded=([bool]$MediaArchiveCache);videoGuiChecks=[int]$videoChecks;videoRuntimeFiles=$mediaLock.files.Count;videoCacheCleaned=$true;chromeGuideVerified=$true;chromeExtensionAutomaticallyLoaded=$false;setupSha256=(Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLower();packageSha256=(Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash.ToLower();version=$manifest.version;localPreview=$manifest.localPreview;normalRegistrationWritten=$false;normalDesktopChanged=$false;userDataPreserved=$true;reinstallVerified=$true;uninstallVerified=$true;runtimePath='Windows system directories only';limitations='Local Windows development machine, not a clean VM or signed release. Chrome text instructions verified without launching/changing normal Chrome; unpublished extension still requires Load unpacked. Fresh upstream downloads are not established when verifiedArchiveCacheSeeded is true.'}
    $report.desktopShortcutVerified=$true;$report.administratorManifestVerified=$true;$report.elevatedProgramFilesInstallVerified=[bool]$ElevatedProduction
    $report.publicYouTubeTransfersVerified=$publicYouTubeVerified;$report.publicYouTubeReport=$YouTubeReportPath
    $report.legacyUserHostMigrationVerified=$true
    $report.legacyDesktopMigrationVerified=$true
    $report.installerOnly=[bool]$InstallerOnly;$report.previousMediaGuiChecks=$previousVideoChecks
    $report.unchangedApplicationSha256=$applicationSha
    if($InstallerOnly){$report.limitations+=' Installer-only mode did not repeat media transfers; previous media acceptance is referenced only after the application SHA-256 and all installed helper hashes match.'}
    $report.verificationSetupSha256=(Get-FileHash -LiteralPath $testSetup -Algorithm SHA256).Hash.ToLower()
    if($ElevatedProduction){$report.limitations+=' The actual production setup installed/reinstalled/uninstalled under Program Files with administrator approval. /ISOLATEDTEST redirected integration to private keys/shortcuts; normal HKLM/Chrome registration and the existing installation were preserved.'}
    else{$report.limitations+=' Production requires administrator approval and defaults to Program Files. Install/reinstall/uninstall ran on a private asInvoker build compiled from the same NSIS source/payload with all integration redirected; no actual elevated machine-wide installation was run.'}
    if($ReportPath){$report|ConvertTo-Json|Set-Content -LiteralPath $ReportPath -Encoding utf8}
    Write-Host "Windows setup checks passed: $checks"
    $succeeded=$true
}finally{
    $env:PATH=$pathBefore;$env:QT_PLUGIN_PATH=$pluginBefore;$env:QT_QPA_PLATFORM=$platformBefore
    if($null -ne $previousTest){$key=$registry.CreateSubKey($testHost);try{$key.SetValue('',$previousTest)}finally{$key.Dispose()}}else{$registry.DeleteSubKey($testHost,$false)}
    if($null -ne $previousAlias){$aliasKey=$registry.CreateSubKey($aliasHost);try{$aliasKey.SetValue('',$previousAlias)}finally{$aliasKey.Dispose()}}else{$registry.DeleteSubKey($aliasHost,$false)}
    $registry.DeleteSubKeyTree($testUninstall,$false);$registry.Dispose()
    $resolved=[IO.Path]::GetFullPath($testRoot)
    if(!$resolved.StartsWith($temporary,[StringComparison]::OrdinalIgnoreCase) -or (Split-Path $resolved -Leaf) -notlike 'Downshift setup test *'){throw 'Unsafe private setup cleanup path.'}
    if($succeeded -and (Test-Path -LiteralPath $resolved)){Remove-Item -LiteralPath $resolved -Recurse -Force}
    elseif(Test-Path -LiteralPath $resolved){Write-Host "Failed isolated setup preserved for diagnostics: $resolved";if(Test-Path -LiteralPath (Join-Path $install 'setup-video.log')){Get-Content -LiteralPath (Join-Path $install 'setup-video.log')|Write-Host}}
}
