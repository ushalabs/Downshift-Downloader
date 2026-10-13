param([string]$ShortcutPath='')
$ErrorActionPreference='Stop'
$root=[System.IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$exe=Join-Path $root 'Downshift.exe'
$hostExe=Join-Path $root 'DownshiftNativeHost.exe'
foreach($file in @($exe,$hostExe)){if(!(Test-Path -LiteralPath $file -PathType Leaf)){throw 'Extract the complete portable ZIP before running setup.'}}
if(!$ShortcutPath){$ShortcutPath=Join-Path ([Environment]::GetFolderPath('Desktop')) 'Downshift.lnk'}
$ShortcutPath=[System.IO.Path]::GetFullPath($ShortcutPath)
$statePath=Join-Path $root 'portable-registration.json'
$manifestPath=Join-Path $root 'org.ushalabs.downshift.json'
$registryPath='Software\Google\Chrome\NativeMessagingHosts\org.ushalabs.downshift'
$registry=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
try {
    if(Test-Path -LiteralPath $statePath){
        $state=Get-Content -LiteralPath $statePath -Raw -Encoding utf8 | ConvertFrom-Json
        if($state.root -ne $root -or $state.shortcut -ne $ShortcutPath){throw 'This setup state belongs to another location. Uninstall there before moving or changing the shortcut.'}
    }else{
        $key=$registry.OpenSubKey($registryPath)
        try{$previous=if($key){$key.GetValue('')}else{$null}}finally{if($key){$key.Dispose()}}
        $shortcutPrevious=$null
        if(Test-Path -LiteralPath $ShortcutPath){$shortcutPrevious=[Convert]::ToBase64String([IO.File]::ReadAllBytes($ShortcutPath))}
        $state=@{root=$root;manifest=$manifestPath;previous=$previous;shortcut=$ShortcutPath;shortcutPrevious=$shortcutPrevious}
        [IO.File]::WriteAllText($statePath,($state|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    }
    $manifest=@{name='org.ushalabs.downshift';description='Downshift local download handoff';path=$hostExe;type='stdio';allowed_origins=@('chrome-extension://empnpdkfnffpcgnfgdndkbielplcoioe/')}
    [IO.File]::WriteAllText($manifestPath,($manifest|ConvertTo-Json -Depth 3),[Text.UTF8Encoding]::new($false))
    $key=$registry.CreateSubKey($registryPath)
    try{$key.SetValue('',$manifestPath,[Microsoft.Win32.RegistryValueKind]::String)}finally{$key.Dispose()}
    $shell=New-Object -ComObject WScript.Shell
    $link=$shell.CreateShortcut($ShortcutPath);$link.TargetPath=$exe;$link.WorkingDirectory=$root;$link.Description='Downshift download manager';$link.Save()
    Write-Host 'Downshift is ready. Open the Desktop shortcut. Load the extension folder from this extracted package in Chrome.'
}finally{$registry.Dispose()}
