param([switch]$IsolatedTest)
$ErrorActionPreference='Stop'
$root=[System.IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$integrationName=if($IsolatedTest){'org.ushalabs.downshift.packaging-test'}else{'org.ushalabs.downshift'}
$statePath=Join-Path $root $(if($IsolatedTest){'portable-registration.test.json'}else{'portable-registration.json'})
if(!(Test-Path -LiteralPath $statePath)){Write-Host 'No portable setup registration to remove.';return}
$state=Get-Content -LiteralPath $statePath -Raw -Encoding utf8 | ConvertFrom-Json
if($state.root -ne $root -or $state.manifest -ne (Join-Path $root ($integrationName+'.json'))){throw 'Setup location changed. Return this folder to its original location before uninstalling.'}
$registryPath='Software\Google\Chrome\NativeMessagingHosts\'+$integrationName
$registry=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
try{
    $key=$registry.OpenSubKey($registryPath)
    try{$current=if($key){$key.GetValue('')}else{$null}}finally{if($key){$key.Dispose()}}
    if($current -eq $state.manifest){
        if($null -ne $state.previous){$key=$registry.CreateSubKey($registryPath);try{$key.SetValue('',$state.previous,[Microsoft.Win32.RegistryValueKind]::String)}finally{$key.Dispose()}}
        else{$registry.DeleteSubKey($registryPath,$false)}
    }
    if(Test-Path -LiteralPath $state.shortcut){
        $shell=New-Object -ComObject WScript.Shell;$link=$shell.CreateShortcut($state.shortcut)
        if($link.TargetPath -eq (Join-Path $root 'Downshift.exe')){
            if($state.shortcutPrevious){[IO.File]::WriteAllBytes($state.shortcut,[Convert]::FromBase64String($state.shortcutPrevious))}
            else{Remove-Item -LiteralPath $state.shortcut}
        }
    }
    Remove-Item -LiteralPath $statePath
    Remove-Item -LiteralPath $state.manifest -ErrorAction SilentlyContinue
    Write-Host 'Portable integration removed. Downloads and encrypted history are preserved. Remove the extension in Chrome. The extracted app folder can then be deleted manually.'
}finally{$registry.Dispose()}
