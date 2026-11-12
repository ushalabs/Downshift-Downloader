param([string]$ApplicationDirectory='', [string]$StateDirectory='', [string]$HostExecutable='', [string]$ChromiumManifestPath='', [switch]$MachineInstall, [switch]$IsolatedTest, [switch]$Unregister)
$ErrorActionPreference='Stop'
if(!$ApplicationDirectory){$ApplicationDirectory=Split-Path $PSScriptRoot -Parent}
$appRoot=[IO.Path]::GetFullPath($ApplicationDirectory)
if(!$StateDirectory){$StateDirectory=$appRoot}
$stateRoot=[IO.Path]::GetFullPath($StateDirectory)
$hostName=if($IsolatedTest){'org.ushalabs.downshift.packaging-test'}else{'org.ushalabs.downshift'}
if(!$HostExecutable){$HostExecutable=Join-Path $appRoot 'DownshiftNativeHost.exe'}
if(!$ChromiumManifestPath){$ChromiumManifestPath=Join-Path $stateRoot ($hostName+'.json')}
$chromium=[IO.Path]::GetFullPath($ChromiumManifestPath)
$firefox=Join-Path $stateRoot ('browser-hosts/Firefox/'+$hostName+'.json')
$receipt=Join-Path $stateRoot $(if($IsolatedTest){'browser-host-registration.test.json'}else{'browser-host-registration.json'})
$hive=if($MachineInstall -and !$IsolatedTest){[Microsoft.Win32.RegistryHive]::LocalMachine}else{[Microsoft.Win32.RegistryHive]::CurrentUser}
$registry=[Microsoft.Win32.RegistryKey]::OpenBaseKey($hive,[Microsoft.Win32.RegistryView]::Registry64)
$entries=@(
    @{key="Software\Microsoft\Edge\NativeMessagingHosts\$hostName";manifest=$chromium},
    @{key="Software\Mozilla\NativeMessagingHosts\$hostName";manifest=$firefox}
)
if($IsolatedTest){foreach($entry in $entries){$entry.key='Software\UshaLabs\Downshift.PackagingTest\BrowserHosts\'+$entry.key}}
function Read-Registration([string]$path){
    $key=$registry.OpenSubKey($path)
    try{if($key){return $key.GetValue('', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)}}finally{if($key){$key.Dispose()}}
    return $null
}
try{
    $saved=$null
    if(Test-Path -LiteralPath $receipt){
        $saved=Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
        if($saved.root -ne $appRoot -or $saved.hive -ne $hive.ToString() -or $saved.entries.Count -ne $entries.Count){throw 'Browser integration belongs to another installation.'}
        for($index=0;$index -lt $entries.Count;$index++){
            if($saved.entries[$index].key -ne $entries[$index].key -or $saved.entries[$index].manifest -ne $entries[$index].manifest){throw 'Unexpected browser integration receipt.'}
        }
    }
    if($Unregister){
        if(!$saved){return}
        foreach($entry in $saved.entries){
            if((Read-Registration $entry.key) -ne $entry.manifest){continue}
            if($null -ne $entry.previous){
                $key=$registry.CreateSubKey($entry.key)
                try{$key.SetValue('',$entry.previous,[Microsoft.Win32.RegistryValueKind]$entry.previousKind)}finally{$key.Dispose()}
            }else{$registry.DeleteSubKey($entry.key,$false)}
        }
        if(Test-Path -LiteralPath $firefox){
            $manifest=Get-Content -LiteralPath $firefox -Raw | ConvertFrom-Json
            if($manifest.name -ne $hostName -or $manifest.path -ne $saved.host){throw 'The Firefox manifest changed and was preserved.'}
            Remove-Item -LiteralPath $firefox -Force
        }
        Remove-Item -LiteralPath $receipt -Force
        foreach($directory in @((Split-Path $firefox -Parent),(Join-Path $stateRoot 'browser-hosts'))){
            if((Test-Path -LiteralPath $directory) -and !(Get-ChildItem -LiteralPath $directory -Force)){Remove-Item -LiteralPath $directory}
        }
        return
    }
    if(!(Test-Path -LiteralPath $HostExecutable -PathType Leaf) -or !(Test-Path -LiteralPath $chromium -PathType Leaf)){throw 'Install the updated Downshift app and Chromium native manifest first.'}
    $chromeHost=Get-Content -LiteralPath $chromium -Raw | ConvertFrom-Json
    if($chromeHost.name -ne $hostName -or $chromeHost.path -ne $HostExecutable){throw 'The Chromium host belongs to a different application.'}
    if(!$saved){
        foreach($entry in $entries){
            $key=$registry.OpenSubKey($entry.key)
            try{
                $entry.previous=if($key){$key.GetValue('', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)}else{$null}
                $entry.previousKind=if($null -ne $entry.previous){$key.GetValueKind('').ToString()}else{'String'}
                if($entry.previousKind -notin @('String','ExpandString')){throw 'An existing browser registration has an unsupported value type.'}
            }finally{if($key){$key.Dispose()}}
        }
        $saved=@{root=$appRoot;hive=$hive.ToString();host=$HostExecutable;entries=$entries}
        [IO.File]::WriteAllText($receipt,($saved | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
    }
    New-Item -ItemType Directory -Path (Split-Path $firefox -Parent) -Force | Out-Null
    $manifest=@{name=$hostName;description='Downshift local download handoff';path=$HostExecutable;type='stdio';allowed_extensions=@('downshift@ushalabs')}
    [IO.File]::WriteAllText($firefox,($manifest | ConvertTo-Json -Depth 3),[Text.UTF8Encoding]::new($false))
    foreach($entry in $entries){
        $key=$registry.CreateSubKey($entry.key)
        try{$key.SetValue('',$entry.manifest,[Microsoft.Win32.RegistryValueKind]::String)}finally{$key.Dispose()}
    }
}finally{$registry.Dispose()}
