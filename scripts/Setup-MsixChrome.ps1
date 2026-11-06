param(
    [Parameter(Mandatory=$true)][string]$StateDirectory,
    [switch]$OpenChrome,
    [switch]$Disconnect
)
$ErrorActionPreference='Stop'
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

    $chromeCandidates=@(
        (Join-Path $env:ProgramFiles 'Google\Chrome\Application\chrome.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Google\Chrome\Application\chrome.exe'),
        (Join-Path $env:LOCALAPPDATA 'Google\Chrome\Application\chrome.exe')
    )
    $chrome=$chromeCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if($OpenChrome -and !$chrome){throw 'Install Google Chrome, then choose Set up Chrome again.'}

    $alias=Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps\DownshiftNativeBridge.exe'
    if(!(Test-Path -LiteralPath $alias -PathType Leaf)){throw 'Enable DownshiftNativeBridge.exe in Windows Settings > App execution aliases, then retry.'}
    New-Item -ItemType Directory -Force -Path $state | Out-Null
    $url=[string]$config.chromeWebStoreUrl
    if($url){
        $expected="https://chromewebstore.google.com/detail/$id"
        if($url -cne $expected){throw 'The Chrome Web Store address does not match this extension.'}
    }else{
        $source=Join-Path $root 'extension'
        $target=Join-Path $state 'ChromeExtension'
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        Get-ChildItem -LiteralPath $source -File -Recurse | ForEach-Object {
            $file=Join-Path $target $_.FullName.Substring($source.Length+1)
            New-Item -ItemType Directory -Force -Path (Split-Path $file -Parent) | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $file -Force
        }
        $url='chrome://extensions/'
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
    if($OpenChrome){Start-Process -FilePath $chrome -ArgumentList $url -WindowStyle Normal}
    if($url -eq 'chrome://extensions/'){Write-Output 'DOWNSHIFT_UNPACKED_READY'}
    else{Write-Output 'DOWNSHIFT_STORE_READY'}
} finally {$registry.Dispose()}
