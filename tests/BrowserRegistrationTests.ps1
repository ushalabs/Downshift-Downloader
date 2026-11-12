$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('Downshift browser registration '+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$name='org.ushalabs.downshift.packaging-test'
$prefix='Software\UshaLabs\Downshift.PackagingTest\BrowserHosts\'
$keys=@(($prefix+'Software\Microsoft\Edge\NativeMessagingHosts\'+$name),($prefix+'Software\Mozilla\NativeMessagingHosts\'+$name))
$registry=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
$checks=0
function Check([bool]$value,[string]$message){if(!$value){throw $message};$script:checks++}
function Fingerprint([string]$path){$hash=[Security.Cryptography.SHA256]::Create();try{return [Convert]::ToBase64String($hash.ComputeHash([IO.File]::ReadAllBytes($path)))}finally{$hash.Dispose()}}
function Value([string]$path){$key=$registry.OpenSubKey($path);try{if($key){return $key.GetValue('')}}finally{if($key){$key.Dispose()}}}
foreach($path in $keys){$existing=$registry.OpenSubKey($path);if($existing){$existing.Dispose();$registry.Dispose();throw 'A previous browser registration fixture exists.'}}
try{
    $hostPath=Join-Path $fixture 'DownshiftNativeHost.exe'
    [IO.File]::WriteAllText($hostPath,'Registration fixture, never executed.')
    @{name=$name;path=$hostPath;type='stdio';allowed_origins=@('chrome-extension://empnpdkfnffpcgnfgdndkbielplcoioe/')} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture ($name+'.json')) -Encoding utf8
    $previous='%TEMP%\Downshift-previous-host.json'
    $key=$registry.CreateSubKey($keys[0]);try{$key.SetValue('',$previous,[Microsoft.Win32.RegistryValueKind]::ExpandString)}finally{$key.Dispose()}
    $script=Join-Path $root 'scripts/Register-BrowserHosts.ps1'
    & $script -ApplicationDirectory $fixture -IsolatedTest
    Check ((Value $keys[0]) -eq (Join-Path $fixture ($name+'.json'))) 'Edge uses the owned Chromium host.'
    $firefox=Join-Path $fixture ('browser-hosts/Firefox/'+$name+'.json')
    Check ((Value $keys[1]) -eq $firefox) 'Firefox has a separate native manifest.'
    $manifest=Get-Content -LiteralPath $firefox -Raw | ConvertFrom-Json
    Check ($manifest.allowed_extensions.Count -eq 1 -and $manifest.allowed_extensions[0] -eq 'downshift@ushalabs') 'Firefox allows only the fixed Downshift add-on ID.'
    Check (!$manifest.allowed_origins -and $manifest.path -eq $hostPath) 'Firefox uses its own manifest format and the correct executable.'
    $receipt=Join-Path $fixture 'browser-host-registration.test.json'
    $receiptHash=Fingerprint $receipt
    & $script -ApplicationDirectory $fixture -IsolatedTest
    Check ((Fingerprint $receipt) -eq $receiptHash) 'Reinstallation preserves the original ownership receipt.'
    $key=$registry.CreateSubKey($keys[1]);try{$key.SetValue('','other-installation.json')}finally{$key.Dispose()}
    & $script -ApplicationDirectory $fixture -IsolatedTest -Unregister
    Check ((Value $keys[1]) -eq 'other-installation.json') 'Uninstall preserves another installation.'
    $key=$registry.OpenSubKey($keys[0])
    try{
        Check ($key.GetValue('', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) -eq $previous) 'Uninstall restores the exact previous registration.'
        Check ($key.GetValueKind('') -eq [Microsoft.Win32.RegistryValueKind]::ExpandString) 'Uninstall restores the previous registry value type.'
    }finally{$key.Dispose()}
    Check (!(Test-Path -LiteralPath $firefox) -and !(Test-Path -LiteralPath $receipt)) 'Owned manifests and receipts are removed.'
    & $script -ApplicationDirectory $fixture -IsolatedTest -Unregister
    Check ((Value $keys[1]) -eq 'other-installation.json') 'Repeated uninstall is safe.'
    Write-Host "Browser registration checks passed: $checks"
}finally{
    foreach($path in $keys){$registry.DeleteSubKey($path,$false)}
    $registry.Dispose()
    $resolved=(Resolve-Path -LiteralPath $fixture).Path
    if(!$resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase) -or (Split-Path $resolved -Leaf) -notlike 'Downshift browser registration *'){throw 'Unexpected fixture cleanup path.'}
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
