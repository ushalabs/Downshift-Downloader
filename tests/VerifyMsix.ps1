param([Parameter(Mandatory=$true)][string]$StorePackage,[Parameter(Mandatory=$true)][string]$CertificateThumbprint,[Parameter(Mandatory=$true)][string]$VerificationDirectory,[string]$VerifiedCache)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$verificationRoot=[IO.Path]::GetFullPath($VerificationDirectory)
if(Test-Path -LiteralPath $verificationRoot){throw 'Choose a new MSIX verification directory.'}
New-Item -ItemType Directory -Path $verificationRoot | Out-Null
$sdk='C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64'
$cert=$CertificateThumbprint
if($cert -notmatch '^[0-9a-fA-F]{40}$'){throw 'Pass the local verification certificate thumbprint.'}
$payload=Join-Path $verificationRoot ('msix-test-'+[guid]::NewGuid().ToString('N'))
$verification=$payload+'.msix'
$name='UshaLabs.Downshift.SetupValidation'
$link=Join-Path ([Environment]::GetFolderPath('Desktop')) 'Downshift Setup Validation.lnk'
if((Get-AppxPackage -Name $name) -or (Test-Path -LiteralPath $link)){throw 'An existing verification fixture would be replaced.'}
function Invoke-Sdk([string]$tool,[string[]]$arguments){
    & (Join-Path $sdk $tool) @arguments | Select-Object -Last 3 | Write-Host
    if($LASTEXITCODE){throw "$tool failed: $LASTEXITCODE"}
}
Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.ComponentModel;
using System.Text;
public static class AppActivation {
  [DllImport("kernel32", SetLastError=true)] private static extern IntPtr OpenProcess(uint access, bool inherit, uint id);
  [DllImport("kernel32", SetLastError=true)] private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
  [DllImport("kernel32", SetLastError=true)] private static extern bool GetExitCodeProcess(IntPtr handle, out uint code);
  [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] private static extern bool QueryFullProcessImageName(IntPtr handle, uint flags, StringBuilder name, ref uint length);
  [DllImport("kernel32")] private static extern bool CloseHandle(IntPtr handle);
  [ComImport, Guid("2e941141-7f97-4756-ba1d-9decde894a3d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  private interface IManager {
    void ActivateApplication([MarshalAs(UnmanagedType.LPWStr)] string id,
      [MarshalAs(UnmanagedType.LPWStr)] string arguments, uint options, out uint processId);
  }
  public static int Check(string id, string arguments, string executable) {
    var manager=(IManager)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("45BA127D-10A8-46EA-8AB7-56EA9078943C")));
    try {
      uint pid;
      manager.ActivateApplication(id,arguments,0,out pid);
      IntPtr handle=OpenProcess(0x101000,false,pid);
      if(handle==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error());
      try {
        uint length=32768;
        var image=new StringBuilder((int)length);
        if(!QueryFullProcessImageName(handle,0,image,ref length))throw new Win32Exception(Marshal.GetLastWin32Error());
        if(!String.Equals(image.ToString(),executable,StringComparison.OrdinalIgnoreCase))
          throw new InvalidOperationException("Activation did not start the installed executable.");
        if(WaitForSingleObject(handle,30000)!=0)throw new TimeoutException("Packaged smoke test timed out.");
        uint code;
        if(!GetExitCodeProcess(handle,out code))throw new Win32Exception(Marshal.GetLastWin32Error());
        return (int)code;
      } finally { CloseHandle(handle); }
    } finally { Marshal.ReleaseComObject(manager); }
  }
}
'@
Invoke-Sdk 'makeappx.exe' @('unpack','/p',$StorePackage,'/d',$payload)
[xml]$manifest=Get-Content -LiteralPath (Join-Path $payload 'AppxManifest.xml') -Raw
if($manifest.Package.Identity.Name -ne 'UshaLabs.Downshift' -or
   $manifest.Package.Identity.Publisher -ne 'CN=844280ED-152B-4FAB-A909-52765F3A4384' -or
   $manifest.Package.Identity.Version -ne '1.0.0.0'){throw 'The Partner Center identity differs.'}
if((Test-Path -LiteralPath (Join-Path $payload 'media-tools')) -or
   !(Test-Path -LiteralPath (Join-Path $payload 'scripts/Install-MediaTools.ps1'))){throw 'The MSIX video payload differs.'}
foreach($browser in @('edge','firefox','opera')){
    $folder=Join-Path $payload ('browser-extensions/'+$browser)
    $browserManifest=Get-Content -LiteralPath (Join-Path $folder 'manifest.json') -Raw | ConvertFrom-Json
    if($browserManifest.version -ne '1.0.0' -or !(Test-Path -LiteralPath (Join-Path $folder 'platform.js'))){throw "Missing or mismatched $browser extension."}
    if($browser -eq 'firefox' -and $browserManifest.browser_specific_settings.gecko.id -ne 'downshift@ushalabs'){throw 'Firefox add-on identity differs.'}
}
foreach($helper in @('Register-BrowserHosts.ps1','Browser-Paths.ps1')){if(!(Test-Path -LiteralPath (Join-Path $payload ('scripts/'+$helper)))){throw 'Browser setup helper missing.'}}
foreach($theme in @('unplated','lightunplated')){
    foreach($size in @(16,20,24,30,32,36,40,48,60,64,72,80,96,256)){
        $asset='StoreAssets/Square44x44Logo.targetsize-{0}_altform-{1}.png' -f $size,$theme
        if(!(Test-Path -LiteralPath (Join-Path $payload $asset))){throw 'A taskbar icon variant is missing.'}
    }
}
$manifest.Package.Identity.Name=$name
$manifest.Package.Identity.Publisher='CN=UshaLabs'
$ns=[Xml.XmlNamespaceManager]::new($manifest.NameTable)
$ns.AddNamespace('shortcut','http://schemas.microsoft.com/appx/manifest/desktop/windows10/7')
$ns.AddNamespace('alias','http://schemas.microsoft.com/appx/manifest/desktop/windows10')
$startup=$manifest.SelectSingleNode('//alias:Extension[@Category="windows.startupTask"]/alias:StartupTask',$ns)
if(!$startup -or $startup.GetAttribute('TaskId') -ne 'DownshiftStartup' -or
   $startup.GetAttribute('Enabled') -ne 'true' -or $startup.ParentNode.GetAttribute('Executable') -ne 'Downshift.exe' -or
   $startup.ParentNode.GetAttribute('Parameters','http://schemas.microsoft.com/appx/manifest/uap/windows10/10') -ne '--background'){
    throw 'The Store startup task is missing or disabled.'
}

$shortcut=$manifest.SelectSingleNode('//shortcut:Shortcut',$ns)
$shortcut.SetAttribute('File',$shortcut.GetAttribute('File').Replace('Downshift (Microsoft Store)','Downshift Setup Validation'))
$manifest.SelectSingleNode('//alias:ExecutionAlias',$ns).SetAttribute('Alias','DownshiftSetupValidationBridge.exe')
$manifest.Save((Join-Path $payload 'AppxManifest.xml'))
Copy-Item -LiteralPath (Join-Path $root 'build/windows-msvc/Release/downshift_gui_tests.exe') -Destination $payload
Invoke-Sdk 'makeappx.exe' @('pack','/d',$payload,'/p',$verification)
Invoke-Sdk 'signtool.exe' @('sign','/fd','SHA256','/sha1',$cert,'/s','My',$verification)
Invoke-Sdk 'signtool.exe' @('verify','/pa',$verification)
$receipt=$payload+'.json'
$runScript=$payload+'.ps1'
@'
param([string]$AppFolder,[string]$Result)
$ErrorActionPreference='Stop'
try{
    $env:QT_QPA_PLATFORM='minimal'
    Add-Type -AssemblyName System.Runtime.WindowsRuntime
    $startupType=[Windows.ApplicationModel.StartupTask,Windows.ApplicationModel,ContentType=WindowsRuntime]
    $operation=$startupType::GetAsync('DownshiftStartup')
    $asTask=[System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetParameters().Count -eq 1 -and
        $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation' + [char]96 + '1'
    } | Select-Object -First 1
    $startup=$asTask.MakeGenericMethod($startupType).Invoke($null,@($operation)).GetAwaiter().GetResult()
    if($startup.State.ToString() -ne 'Enabled'){throw "Windows reported startup state $($startup.State)"}

    $process=Start-Process -FilePath (Join-Path $AppFolder 'downshift_gui_tests.exe') -ArgumentList '--first-launch-setup' -WindowStyle Hidden -PassThru -RedirectStandardOutput ($Result+'.stdout') -RedirectStandardError ($Result+'.stderr')
    if(!$process.WaitForExit(600000)){throw 'First launch timed out'}
    if($process.ExitCode){throw (Get-Content -LiteralPath ($Result+'.stderr') -Raw)}
    $smoke=Start-Process -FilePath (Join-Path $AppFolder 'Downshift.exe') -ArgumentList '--smoke-test' -WindowStyle Hidden -PassThru
    if(!$smoke.WaitForExit(20000) -or $smoke.ExitCode){throw 'App startup check failed'}
    $backgroundSmoke=Start-Process -FilePath (Join-Path $AppFolder 'Downshift.exe') -ArgumentList '--smoke-test --background' -WindowStyle Hidden -PassThru
    if(!$backgroundSmoke.WaitForExit(20000) -or $backgroundSmoke.ExitCode){throw 'Tray-only packaged startup failed'}
    @{ok=$true;signInStartup=$startup.State.ToString();firstLaunch=(Get-Content -LiteralPath ($Result+'.stdout') -Raw)}|ConvertTo-Json|Set-Content -LiteralPath $Result
}catch{@{ok=$false;error=$_.Exception.Message}|ConvertTo-Json|Set-Content -LiteralPath $Result}
'@ | Set-Content -LiteralPath $runScript -Encoding utf8
try{
    Add-AppxPackage -Path $verification
    $app=Get-AppxPackage -Name $name
    if(!$app.InstallLocation.StartsWith((Join-Path $env:ProgramFiles 'WindowsApps'),[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected MSIX installation location.'}
    $activation=[AppActivation]::Check($app.PackageFamilyName+'!Downshift','--smoke-test',(Join-Path $app.InstallLocation 'Downshift.exe'))
    if($activation -ne 0){throw "Shell activation returned $activation"}
    $state=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) ('Packages/'+$app.PackageFamilyName+'/LocalState')
    if($VerifiedCache){
        $cache=Join-Path $state 'media-cache'
        New-Item -ItemType Directory -Path $cache -Force | Out-Null
        $assets=(Get-Content -LiteralPath (Join-Path $payload 'scripts/media-tools-lock.json') -Raw | ConvertFrom-Json).assets
        foreach($asset in $assets){
            $archive=Join-Path $VerifiedCache $asset.name
            if((Get-FileHash -LiteralPath $archive).Hash -ne $asset.sha256){throw "Wrong test archive: $($asset.name)"}
            Copy-Item -LiteralPath $archive -Destination $cache
        }
    }
    if(!(Test-Path -LiteralPath $link)){throw 'The MSIX Desktop shortcut was not created.'}
    $lnk=(New-Object -ComObject WScript.Shell).CreateShortcut($link)
    if($lnk.TargetPath -ne (Join-Path $app.InstallLocation 'Downshift.exe') -or $lnk.IconLocation -notlike '*Downshift.exe,*'){throw 'The Desktop shortcut does not use the packaged Downshift target and icon.'}
    $powershell=Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $args='-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "'+$runScript+'" -AppFolder "'+$app.InstallLocation+'" -Result "'+$receipt+'"'
    Invoke-CommandInDesktopPackage -PackageFamilyName $app.PackageFamilyName -AppId Downshift -Command $powershell -Args $args -PreventBreakaway
    $deadline=[DateTime]::UtcNow.AddSeconds(650)
    while(!(Test-Path -LiteralPath $receipt) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 300}
    $result=Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
    if(!$result.ok){throw "MSIX first launch failed: $($result.error)"}
    Write-Host $result.firstLaunch
    Write-Host "Windows StartupTask state after first activation: $($result.signInStartup)"
    $toolLock=Get-Content -LiteralPath (Join-Path $payload 'scripts/media-tools-lock.json') -Raw | ConvertFrom-Json
    foreach($file in $toolLock.files){
        if((Get-FileHash -LiteralPath (Join-Path $state ('media-tools/'+$file.name)) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256){throw "MSIX helper differs: $($file.name)"}
    }
    if(Test-Path -LiteralPath (Join-Path $state 'media-cache')){throw 'MSIX retained the setup cache.'}
}finally{
    $app=Get-AppxPackage -Name $name
    if($app){Remove-AppxPackage -Package $app.PackageFullName}
}
if((Get-AppxPackage -Name $name) -or (Test-Path -LiteralPath $link)){throw 'MSIX uninstall retained the package or its Desktop shortcut.'}
Write-Host 'Verified signed MSIX, exact Store identity, unplated assets, private install, first launch and uninstall.'
@{packageSha256=(Get-FileHash -LiteralPath $StorePackage -Algorithm SHA256).Hash.ToLowerInvariant();signedPrivateInstallationVerified=$true;startupParameters='--background';startupEnabled=$true;trayStartupVerified=$true;uninstallVerified=$true} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $verificationRoot 'msix-verified.json') -Encoding utf8
