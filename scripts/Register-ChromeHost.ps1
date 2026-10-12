param([ValidateSet('Release','Debug')][string]$Configuration='Release',[switch]$Unregister)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$hostName='org.ushalabs.downshift'
$registryPath="Software\Google\Chrome\NativeMessagingHosts\$hostName"
$registry=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
try {
    if ($Unregister) { $registry.DeleteSubKey($registryPath,$false); Write-Host 'Downshift Chrome host registration removed for this Windows account.'; return }
    $hostExecutable=(Resolve-Path -LiteralPath (Join-Path $projectRoot "build\windows-msvc\$Configuration\DownshiftNativeHost.exe")).Path
    $manifestPath=Join-Path (Split-Path $hostExecutable -Parent) "$hostName.json"
    $manifest=@{ name=$hostName; description='Downshift local download handoff'; path=$hostExecutable; type='stdio'; allowed_origins=@('chrome-extension://empnpdkfnffpcgnfgdndkbielplcoioe/') }
    [System.IO.File]::WriteAllText($manifestPath,($manifest | ConvertTo-Json -Depth 3),[System.Text.UTF8Encoding]::new($false))
    $key=$registry.CreateSubKey($registryPath)
    try { $key.SetValue('',$manifestPath,[Microsoft.Win32.RegistryValueKind]::String) } finally { $key.Dispose() }
    Write-Host 'Downshift Chrome host registered for this Windows account. No administrator rights are required.'
    Write-Host "Load the extension folder in Chrome developer mode: $(Join-Path $projectRoot 'extension')"
} finally { $registry.Dispose() }
