param([string]$ShortcutPath='', [string]$AdditionalShortcutPath='', [switch]$MachineInstall, [switch]$IsolatedTest)
$ErrorActionPreference='Stop'
$root=[System.IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$exe=Join-Path $root 'Downshift.exe'
$hostExe=Join-Path $root 'DownshiftNativeHost.exe'
foreach($file in @($exe,$hostExe)){if(!(Test-Path -LiteralPath $file -PathType Leaf)){throw 'Extract the complete portable ZIP before running setup.'}}
if($IsolatedTest -and !$ShortcutPath){throw 'Isolated integration tests require an explicit temporary shortcut path.'}
if(!$ShortcutPath){$ShortcutPath=Join-Path ([Environment]::GetFolderPath('Desktop')) 'Downshift.lnk'}
$ShortcutPath=[System.IO.Path]::GetFullPath($ShortcutPath)
if($AdditionalShortcutPath){$AdditionalShortcutPath=[IO.Path]::GetFullPath($AdditionalShortcutPath);if($AdditionalShortcutPath -eq $ShortcutPath){throw 'Shortcut paths must be different.'}}
$hive=if($MachineInstall -and !$IsolatedTest){[Microsoft.Win32.RegistryHive]::LocalMachine}else{[Microsoft.Win32.RegistryHive]::CurrentUser}
$integrationName=if($IsolatedTest){'org.ushalabs.downshift.packaging-test'}else{'org.ushalabs.downshift'}
$statePath=Join-Path $root $(if($IsolatedTest){'portable-registration.test.json'}else{'portable-registration.json'})
$manifestPath=Join-Path $root ($integrationName+'.json')
$registryPath='Software\Google\Chrome\NativeMessagingHosts\'+$integrationName
$registry=[Microsoft.Win32.RegistryKey]::OpenBaseKey($hive,[Microsoft.Win32.RegistryView]::Registry64)
try {
    if(Test-Path -LiteralPath $statePath){
        $state=Get-Content -LiteralPath $statePath -Raw -Encoding utf8 | ConvertFrom-Json
        if($state.root -ne $root -or $state.shortcut -ne $ShortcutPath){throw 'This setup state belongs to another location. Uninstall there before moving or changing the shortcut.'}
        $previousHive=if($state.registryHive){$state.registryHive}else{'CurrentUser'}
        if($previousHive -ne $hive.ToString()){throw 'This integration belongs to a different Windows installation scope.'}
    }else{
        $key=$registry.OpenSubKey($registryPath)
        try{$previous=if($key){$key.GetValue('')}else{$null}}finally{if($key){$key.Dispose()}}
        $shortcutPrevious=$null
        if(Test-Path -LiteralPath $ShortcutPath){$shortcutPrevious=[Convert]::ToBase64String([IO.File]::ReadAllBytes($ShortcutPath))}
        $state=[pscustomobject]@{root=$root;manifest=$manifestPath;previous=$previous;shortcut=$ShortcutPath;shortcutPrevious=$shortcutPrevious;registryHive=$hive.ToString()}
        [IO.File]::WriteAllText($statePath,($state|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    }
    if($AdditionalShortcutPath){
        if($state.additionalShortcut -and $state.additionalShortcut -ne $AdditionalShortcutPath){throw 'The saved additional shortcut belongs to another location.'}
        if(!$state.additionalShortcut){
            $additionalPrevious=if(Test-Path -LiteralPath $AdditionalShortcutPath){[Convert]::ToBase64String([IO.File]::ReadAllBytes($AdditionalShortcutPath))}else{$null}
            $state | Add-Member -NotePropertyName additionalShortcut -NotePropertyValue $AdditionalShortcutPath -Force
            $state | Add-Member -NotePropertyName additionalShortcutPrevious -NotePropertyValue $additionalPrevious -Force
            [IO.File]::WriteAllText($statePath,($state|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
        }
    }
    $manifest=@{name=$integrationName;description='Downshift local download handoff';path=$hostExe;type='stdio';allowed_origins=@('chrome-extension://empnpdkfnffpcgnfgdndkbielplcoioe/')}
    [IO.File]::WriteAllText($manifestPath,($manifest|ConvertTo-Json -Depth 3),[Text.UTF8Encoding]::new($false))
    $key=$registry.CreateSubKey($registryPath)
    try{$key.SetValue('',$manifestPath,[Microsoft.Win32.RegistryValueKind]::String)}finally{$key.Dispose()}
    if($MachineInstall){
        # Chrome checks HKCU before HKLM. A previous development/portable host
        # must not keep rejecting the updated application's media protocol.
        # Private verification uses a separate alias namespace, never the real key.
        $aliasPath=if($IsolatedTest){$registryPath+'.user-override'}else{$registryPath}
        $userRegistry=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
        try{
            $alias=$userRegistry.OpenSubKey($aliasPath)
            try{$oldAlias=if($alias){$alias.GetValue('')}else{$null}}finally{if($alias){$alias.Dispose()}}
            if($oldAlias -and $oldAlias -ne $manifestPath){
                $owned=$false
                if((Test-Path -LiteralPath $oldAlias -PathType Leaf) -and (Get-Item -LiteralPath $oldAlias).Length -lt 32768){
                    try{
                        $old=Get-Content -LiteralPath $oldAlias -Raw -Encoding utf8|ConvertFrom-Json
                        $owned=$old.name -eq $integrationName -and $old.type -eq 'stdio' -and @($old.allowed_origins).Count -eq 1 -and $old.allowed_origins[0] -eq 'chrome-extension://empnpdkfnffpcgnfgdndkbielplcoioe/' -and [IO.Path]::GetFileName($old.path) -eq 'DownshiftNativeHost.exe' -and [IO.Path]::GetDirectoryName($old.path) -eq [IO.Path]::GetDirectoryName($oldAlias) -and (Test-Path -LiteralPath $old.path -PathType Leaf)
                    }catch{$owned=$false}
                }
                if(!$owned){throw 'An unrecognized per-user browser host overrides Downshift. Its registration was preserved.'}
                if(!$state.currentUserAliasPath){
                    $state|Add-Member -NotePropertyName currentUserAliasPath -NotePropertyValue $aliasPath -Force
                    $state|Add-Member -NotePropertyName currentUserAliasPrevious -NotePropertyValue $oldAlias -Force
                    $state|Add-Member -NotePropertyName currentUserAliasSid -NotePropertyValue ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -Force
                    [IO.File]::WriteAllText($statePath,($state|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
                }
                $alias=$userRegistry.CreateSubKey($aliasPath);try{$alias.SetValue('',$manifestPath,[Microsoft.Win32.RegistryValueKind]::String)}finally{$alias.Dispose()}
                $legacyDesktop=if($IsolatedTest){Join-Path ([IO.Path]::GetDirectoryName($old.path)) 'Downshift.lnk'}else{Join-Path ([Environment]::GetFolderPath('Desktop')) 'Downshift.lnk'}
                if((Test-Path -LiteralPath $legacyDesktop -PathType Leaf) -and $legacyDesktop -ne $ShortcutPath -and $legacyDesktop -ne $AdditionalShortcutPath){
                    $legacyShell=New-Object -ComObject WScript.Shell;$legacyLink=$legacyShell.CreateShortcut($legacyDesktop)
                    if($legacyLink.TargetPath -eq (Join-Path ([IO.Path]::GetDirectoryName($old.path)) 'Downshift.exe')){
                        if(!$state.currentUserShortcutPath){
                            $state|Add-Member -NotePropertyName currentUserShortcutPath -NotePropertyValue $legacyDesktop -Force
                            $state|Add-Member -NotePropertyName currentUserShortcutPrevious -NotePropertyValue ([Convert]::ToBase64String([IO.File]::ReadAllBytes($legacyDesktop))) -Force
                            [IO.File]::WriteAllText($statePath,($state|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
                        }
                        $legacyLink.TargetPath=$exe;$legacyLink.WorkingDirectory=$root;$legacyLink.IconLocation="$exe,0";$legacyLink.Save()
                    }
                }
            }
        }finally{$userRegistry.Dispose()}
    }
    $shell=New-Object -ComObject WScript.Shell
    foreach($shortcut in @($ShortcutPath,$AdditionalShortcutPath) | Where-Object {$_}){
        $link=$shell.CreateShortcut($shortcut);$link.TargetPath=$exe;$link.WorkingDirectory=$root;$link.IconLocation="$exe,0";$link.Description='Downshift download manager';$link.Save()
    }
    Write-Host 'Downshift is ready. Open the Desktop shortcut. Load the extension folder from this extracted package in Chrome.'
}finally{$registry.Dispose()}
