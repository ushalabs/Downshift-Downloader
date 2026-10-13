param([Parameter(Mandatory=$true)][string]$PackagePath,[string]$ReportPath='')
$ErrorActionPreference='Stop'
$package=(Resolve-Path -LiteralPath $PackagePath).Path
$tempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot=Join-Path $tempRoot ('Downshift portable test '+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$registryPath='Software\Google\Chrome\NativeMessagingHosts\org.ushalabs.downshift'
$registry=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
function Read-Registration {
    $key=$registry.OpenSubKey($registryPath)
    try{if($key){return $key.GetValue('')}}finally{if($key){$key.Dispose()}}
    return $null
}
$previous=Read-Registration
$savedPath=$env:PATH;$savedPlugin=$env:QT_PLUGIN_PATH;$savedPlatform=$env:QT_QPA_PLATFORM
$checks=0;$process=$null
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($package,$testRoot)
    $app=Get-ChildItem -LiteralPath $testRoot -Directory | Select-Object -First 1
    if(!$app){throw 'No app folder in ZIP.'}
    $root=$app.FullName
    $manifest=Get-Content -LiteralPath (Join-Path $root 'package-manifest.json') -Raw -Encoding utf8|ConvertFrom-Json
    foreach($file in $manifest.files){
        $path=[IO.Path]::GetFullPath((Join-Path $root $file.path))
        if(!$path.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Manifest path escaped app folder.'}
        if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower() -ne $file.sha256){throw "Package integrity failure: $($file.path)"}
    };$checks++
    foreach($required in @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll','libcurl.dll','sqlite3.dll','platforms/qwindows.dll','licenses/qtbase/copyright')){if(!(Test-Path -LiteralPath (Join-Path $root $required))){throw "Missing runtime or license: $required"}};$checks++
    if(Test-Path -LiteralPath (Join-Path $root 'media-tools')){throw 'Optional upstream media executables must not be redistributed in this package.'};$checks++
    $extension=Get-Content -LiteralPath (Join-Path $root 'extension/manifest.json') -Raw|ConvertFrom-Json
    if($extension.version -ne $manifest.version){throw 'Extension/product version mismatch.'};$checks++
    $shortcut=Join-Path $testRoot 'Downshift.lnk';$shell=New-Object -ComObject WScript.Shell
    $oldLink=$shell.CreateShortcut($shortcut);$oldLink.TargetPath=Join-Path $env:WINDIR 'notepad.exe';$oldLink.Save()
    $oldBytes=[Convert]::ToBase64String([IO.File]::ReadAllBytes($shortcut))
    & (Join-Path $root 'scripts/Install-Portable.ps1') -ShortcutPath $shortcut
    & (Join-Path $root 'scripts/Install-Portable.ps1') -ShortcutPath $shortcut
    $registered=Read-Registration
    if($registered -ne (Join-Path $root 'org.ushalabs.downshift.json')){throw 'Native host registration failed.'};$checks++
    $hostManifest=Get-Content -LiteralPath $registered -Raw|ConvertFrom-Json
    if($hostManifest.path -ne (Join-Path $root 'DownshiftNativeHost.exe') -or $hostManifest.allowed_origins.Count -ne 1 -or $hostManifest.allowed_origins[0] -ne 'chrome-extension://empnpdkfnffpcgnfgdndkbielplcoioe/'){throw 'Invalid native host manifest.'};$checks++
    $link=$shell.CreateShortcut($shortcut)
    if($link.TargetPath -ne (Join-Path $root 'Downshift.exe') -or $link.WorkingDirectory -ne $root){throw 'Shortcut target mismatch.'};$checks++
    # The actual packaged window loads with developer paths removed. Smoke mode uses
    # an isolated temporary database and never owns the user's normal browser pipe.
    $env:PATH="$env:WINDIR\System32;$env:WINDIR";$env:QT_PLUGIN_PATH='';$env:QT_QPA_PLATFORM='windows'
    $process=Start-Process -FilePath (Join-Path $root 'Downshift.exe') -ArgumentList '--smoke-test' -WorkingDirectory $testRoot -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $testRoot 'startup-error.txt')
    $null=$process.Handle
    $requiredModules=@('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','libcurl.dll','sqlite3.dll','msvcp140.dll','vcruntime140.dll')
    $seen=@{};$deadline=[DateTime]::UtcNow.AddSeconds(8)
    do {
        $process.Refresh();if($process.HasExited){break}
        foreach($module in @($process.Modules)){if($requiredModules -contains $module.ModuleName){$seen[$module.ModuleName]=$module.FileName}}
        if($seen.Count -eq $requiredModules.Count){break}
        Start-Sleep -Milliseconds 25
    }while([DateTime]::UtcNow -lt $deadline)
    foreach($dll in $requiredModules){
        if($seen[$dll] -ne (Join-Path $root $dll)){throw "Runtime did not load from portable folder: $dll"}
    };$checks++
    if(!$process.WaitForExit(15000)){throw 'Packaged app startup timed out.'}
    $process.Refresh()
    if($process.ExitCode -ne 0){throw "Packaged startup failed ($($process.ExitCode)): $(Get-Content (Join-Path $testRoot 'startup-error.txt') -Raw)"};$checks++
    $env:PATH=$savedPath;$env:QT_PLUGIN_PATH=$savedPlugin;$env:QT_QPA_PLATFORM=$savedPlatform
    & (Join-Path $root 'scripts/Uninstall-Portable.ps1')
    if((Read-Registration) -ne $previous){throw 'Uninstall failed to restore previous registration.'};$checks++
    if([Convert]::ToBase64String([IO.File]::ReadAllBytes($shortcut)) -ne $oldBytes){throw 'Uninstall failed to restore previous shortcut.'};$checks++
    if(!(Test-Path -LiteralPath (Join-Path $root 'Downshift.exe'))){throw 'Uninstall removed portable binaries unexpectedly.'};$checks++
    # A later installation must not be clobbered by this one's uninstall.
    & (Join-Path $root 'scripts/Install-Portable.ps1') -ShortcutPath $shortcut
    $key=$registry.CreateSubKey($registryPath);try{$key.SetValue('','other-installation.json')}finally{$key.Dispose()}
    $other=$shell.CreateShortcut($shortcut);$other.TargetPath=Join-Path $env:WINDIR 'notepad.exe';$other.Save()
    & (Join-Path $root 'scripts/Uninstall-Portable.ps1')
    if((Read-Registration) -ne 'other-installation.json' -or $shell.CreateShortcut($shortcut).TargetPath -ne (Join-Path $env:WINDIR 'notepad.exe')){throw 'Uninstall changed another installation.'};$checks++
    $report=@{checks=$checks;version=$manifest.version;packageSha256=(Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash.ToLower();os=[Environment]::OSVersion.VersionString;sourceCommit=$manifest.sourceCommit;runtimePath='Windows system directories only';scope='ZIP integrity; app-local runtime; version; idempotent per-user setup; host origin/path; shortcut; actual GUI startup without developer PATH; uninstall restores previous registration/shortcut and preserves later ownership';limitations='same development machine with sanitized PATH, not a clean Windows VM; optional media helpers separately verified by source media tests'}
    if($ReportPath){$report|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $ReportPath -Encoding utf8}
    Write-Host "Portable checks passed: $checks"
}finally{
    $env:PATH=$savedPath;$env:QT_PLUGIN_PATH=$savedPlugin;$env:QT_QPA_PLATFORM=$savedPlatform
    if($process -and !$process.HasExited){$process.Kill();$process.WaitForExit()}
    if($null -ne $previous){$key=$registry.CreateSubKey($registryPath);try{$key.SetValue('',$previous,[Microsoft.Win32.RegistryValueKind]::String)}finally{$key.Dispose()}}
    else{$registry.DeleteSubKey($registryPath,$false)}
    $registry.Dispose()
    $resolved=[IO.Path]::GetFullPath($testRoot)
    if(!$resolved.StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -or (Split-Path $resolved -Leaf) -notlike 'Downshift portable test *'){throw 'Refusing unsafe cleanup path.'}
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
