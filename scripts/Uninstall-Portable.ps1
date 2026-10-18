param([switch]$IsolatedTest,[switch]$MachineInstall)
$ErrorActionPreference='Stop'
$root=[System.IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$integrationName=if($IsolatedTest){'org.ushalabs.downshift.packaging-test'}else{'org.ushalabs.downshift'}
$statePath=Join-Path $root $(if($IsolatedTest){'portable-registration.test.json'}else{'portable-registration.json'})
if(!(Test-Path -LiteralPath $statePath)){Write-Host 'No portable setup registration to remove.';return}
$state=Get-Content -LiteralPath $statePath -Raw -Encoding utf8 | ConvertFrom-Json
if($state.root -ne $root -or $state.manifest -ne (Join-Path $root ($integrationName+'.json'))){throw 'Setup location changed. Return this folder to its original location before uninstalling.'}
$registryPath='Software\Google\Chrome\NativeMessagingHosts\'+$integrationName
$hive=if($MachineInstall -and !$IsolatedTest){[Microsoft.Win32.RegistryHive]::LocalMachine}else{[Microsoft.Win32.RegistryHive]::CurrentUser}
$savedHive=if($state.registryHive){$state.registryHive}else{'CurrentUser'}
if($savedHive -ne $hive.ToString()){throw 'The saved integration belongs to a different Windows installation scope.'}
$registry=[Microsoft.Win32.RegistryKey]::OpenBaseKey($hive,[Microsoft.Win32.RegistryView]::Registry64)
try{
    $key=$registry.OpenSubKey($registryPath)
    try{$current=if($key){$key.GetValue('')}else{$null}}finally{if($key){$key.Dispose()}}
    if($current -eq $state.manifest){
        if($null -ne $state.previous){$key=$registry.CreateSubKey($registryPath);try{$key.SetValue('',$state.previous,[Microsoft.Win32.RegistryValueKind]::String)}finally{$key.Dispose()}}
        else{$registry.DeleteSubKey($registryPath,$false)}
    }
    if($state.currentUserAliasPath){
        $expectedAlias=if($IsolatedTest){$registryPath+'.user-override'}else{$registryPath}
        if($state.currentUserAliasPath -ne $expectedAlias){throw 'The saved per-user browser registration belongs to another integration.'}
        if($state.currentUserAliasSid -eq [Security.Principal.WindowsIdentity]::GetCurrent().User.Value){
            $userRegistry=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
            try{
                $alias=$userRegistry.OpenSubKey($expectedAlias)
                try{$aliasValue=if($alias){$alias.GetValue('')}else{$null}}finally{if($alias){$alias.Dispose()}}
                if($aliasValue -eq $state.manifest){$alias=$userRegistry.CreateSubKey($expectedAlias);try{$alias.SetValue('',$state.currentUserAliasPrevious,[Microsoft.Win32.RegistryValueKind]::String)}finally{$alias.Dispose()}}
            }finally{$userRegistry.Dispose()}
        }
    }
    $shortcuts=@(@{path=$state.shortcut;previous=$state.shortcutPrevious})
    if($state.currentUserShortcutPath -and $state.currentUserAliasSid -eq [Security.Principal.WindowsIdentity]::GetCurrent().User.Value){$shortcuts+=@{path=$state.currentUserShortcutPath;previous=$state.currentUserShortcutPrevious}}
    if($state.additionalShortcut){$shortcuts+=@{path=$state.additionalShortcut;previous=$state.additionalShortcutPrevious}}
    foreach($shortcut in $shortcuts){if(Test-Path -LiteralPath $shortcut.path){
        $shell=New-Object -ComObject WScript.Shell;$link=$shell.CreateShortcut($shortcut.path)
        if($link.TargetPath -eq (Join-Path $root 'Downshift.exe')){
            if($shortcut.previous){[IO.File]::WriteAllBytes($shortcut.path,[Convert]::FromBase64String($shortcut.previous))}
            else{Remove-Item -LiteralPath $shortcut.path}
        }
    }}
    Remove-Item -LiteralPath $statePath
    Remove-Item -LiteralPath $state.manifest -ErrorAction SilentlyContinue
    Write-Host 'Portable integration removed. Downloads and encrypted history are preserved. Remove the extension in Chrome. The extracted app folder can then be deleted manually.'
}finally{$registry.Dispose()}
