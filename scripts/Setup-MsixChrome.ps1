param(
    [Parameter(Mandatory=$true)][string]$StateDirectory,
    [switch]$OpenChrome,
    [switch]$Disconnect,
    [ValidateSet('Chrome','Edge','Firefox','Opera')][string]$Browser='Chrome'
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Browser-Paths.ps1')
$root=Split-Path $PSScriptRoot -Parent
$config=Get-Content -LiteralPath (Join-Path $root 'msix-setup.json') -Raw | ConvertFrom-Json
$id='empnpdkfnffpcgnfgdndkbielplcoioe'
$hostName='org.ushalabs.downshift'
$chromeKey="Software\Google\Chrome\NativeMessagingHosts\$hostName"
$state=[IO.Path]::GetFullPath($StateDirectory)
$nativeManifest=Join-Path $state "$hostName.json"
$previousRegistration=Join-Path $state 'previous-chrome-registration.json'
$registry=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)

function Write-SetupJson($value,[string]$path) {
    $temporary=$path+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    try {
        [IO.File]::WriteAllText($temporary,($value | ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $temporary -Destination $path -Force
    } finally {
        if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary -Force}
    }
}

try {
    $current=$registry.OpenSubKey($chromeKey)
    $existing=$null
    try {
        if($current){$existing=$current.GetValue('', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)}
    } finally {
        if($current){$current.Dispose()}
    }
    if($Disconnect){
        & (Join-Path $PSScriptRoot 'Register-BrowserHosts.ps1') -ApplicationDirectory $root -StateDirectory $state -Unregister
        if($existing -eq $nativeManifest){
            $prior=Get-Content -LiteralPath $previousRegistration -Raw | ConvertFrom-Json
            $key=$registry.OpenSubKey($chromeKey,$true)
            try {
                if($null -eq $prior.path){$key.DeleteValue('',$false)}
                else{$key.SetValue('',[string]$prior.path,[Microsoft.Win32.RegistryValueKind]$prior.kind)}
            } finally {$key.Dispose()}
        }
        return
    }

    $chrome=Find-DownshiftBrowser $Browser
    if($OpenChrome -and !$chrome){throw "Install $Browser, then choose browser setup again."}

    $alias=Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps\DownshiftNativeBridge.exe'
    if(!(Test-Path -LiteralPath $alias -PathType Leaf)){throw 'Enable DownshiftNativeBridge.exe in Windows Settings > App execution aliases, then retry.'}
    New-Item -ItemType Directory -Force -Path $state | Out-Null
    $url=[string]$config.chromeWebStoreUrl
    if($Browser -ne 'Chrome'){$url=''}
    if($url){
        $expected="https://chromewebstore.google.com/detail/$id"
        if($url -cne $expected){throw 'The Chrome Web Store address does not match this extension.'}
    }else{
        $source=Join-Path $root 'extension'
        $target=Join-Path $state 'ChromeExtension'
        if($Browser -ne 'Chrome'){
            $source=Join-Path $root ('browser-extensions/'+$Browser.ToLowerInvariant())
            $target=Join-Path $state ('BrowserExtensions/'+$Browser.ToLowerInvariant())
        }
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        Get-ChildItem -LiteralPath $source -File -Recurse | ForEach-Object {
            $file=Join-Path $target $_.FullName.Substring($source.Length+1)
            New-Item -ItemType Directory -Force -Path (Split-Path $file -Parent) | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $file -Force
        }
        $url='chrome://extensions/'
        if($Browser -ne 'Chrome'){$url=switch($Browser){'Edge'{'edge://extensions/'}'Firefox'{'about:debugging#/runtime/this-firefox'}'Opera'{'opera://extensions/'}}}
    }
    Write-SetupJson @{name=$hostName;description='Downshift local download handoff';path=$alias;type='stdio';allowed_origins=@("chrome-extension://$id/")} $nativeManifest
    if($existing -ne $nativeManifest){
        $key=$registry.CreateSubKey($chromeKey)
        try {
            $kind=if($null -ne $existing){$key.GetValueKind('').ToString()}else{'String'}
            if($kind -notin @('String','ExpandString')){throw 'The existing Chrome connection uses an unsupported registry value.'}
            Write-SetupJson @{path=$existing;kind=$kind} $previousRegistration
            $key.SetValue('',$nativeManifest,[Microsoft.Win32.RegistryValueKind]::String)
        } finally {$key.Dispose()}
    }
    & (Join-Path $PSScriptRoot 'Register-BrowserHosts.ps1') -ApplicationDirectory $root -StateDirectory $state -HostExecutable $alias -ChromiumManifestPath $nativeManifest
    if($OpenChrome){Start-Process -FilePath $chrome -ArgumentList $url -WindowStyle Normal}
    if(!$config.chromeWebStoreUrl -or $Browser -ne 'Chrome'){Write-Output 'DOWNSHIFT_UNPACKED_READY'}
    else{Write-Output 'DOWNSHIFT_STORE_READY'}
} finally {$registry.Dispose()}
