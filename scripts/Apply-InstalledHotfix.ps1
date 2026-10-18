param(
    [Parameter(Mandatory=$true)][string]$PayloadDirectory,
    [Parameter(Mandatory=$true)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ManifestSha256,
    [string]$TargetDirectory='',
    [switch]$IsolatedFixture,
    [switch]$FailAfterFirstSwap
)
$ErrorActionPreference='Stop'
$payload=[IO.Path]::GetFullPath($PayloadDirectory)
$manifestPath=Join-Path $payload 'hotfix.json'
$reportPath=Join-Path (Split-Path $payload -Parent) 'applied-report.json'
function Hash([string]$path){return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower()}
function No-Link([string]$path){
    $item=Get-Item -LiteralPath $path -Force
    if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked update path refused: $path"}
}
try {
    if((Hash $manifestPath) -ne $ManifestSha256.ToLower()){throw 'Hotfix manifest hash differs.'}
    $manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding utf8|ConvertFrom-Json
    if($manifest.schema -ne 1 -or $manifest.product -ne 'Downshift' -or @($manifest.files).Count -ne 5){throw 'Invalid hotfix manifest.'}
    $names=@('Downshift.exe','build-source.json','extension/youtube-player.js','extension/background.js','extension/video-button.js')
    if((@($manifest.files|ForEach-Object {$_.path}|Sort-Object) -join '|') -ne (($names|Sort-Object) -join '|')){throw 'Hotfix contains unexpected files.'}
    $programFiles=if($env:ProgramW6432){$env:ProgramW6432}else{$env:ProgramFiles}
    $normalTarget=[IO.Path]::GetFullPath((Join-Path $programFiles 'Downshift'))
    if(!$TargetDirectory){$TargetDirectory=$normalTarget}
    $target=[IO.Path]::GetFullPath($TargetDirectory).TrimEnd('\')
    if($IsolatedFixture){
        $fixtureRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../build/installed-hotfix-verification'))
        if(!$target.StartsWith($fixtureRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath (Join-Path $target '.hotfix-fixture'))){throw 'Private hotfix fixture is outside its declared root.'}
    }else{
        if($target -ne $normalTarget -or $FailAfterFirstSwap){throw 'Production hotfix target differs.'}
        $principal=[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
        if(!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Administrator rights are required to update Program Files.'}
    }
    No-Link $target;No-Link (Join-Path $target 'extension');No-Link $payload
    $restartRequired=@(Get-Process Downshift -ErrorAction SilentlyContinue|Where-Object {$_.Path -eq (Join-Path $target 'Downshift.exe')}).Count -gt 0
    foreach($entry in $manifest.files){
        if($entry.before -notmatch '^[a-f0-9]{64}$' -or $entry.after -notmatch '^[a-f0-9]{64}$'){throw 'Invalid file hash.'}
        $old=Join-Path $target $entry.path;$new=Join-Path $payload $entry.path
        No-Link $old;No-Link $new
        if((Hash $old) -ne $entry.before){throw "Installed file changed since hotfix preparation: $($entry.path)"}
        if((Hash $new) -ne $entry.after){throw "Incoming file hash differs: $($entry.path)"}
    }
    $token=[guid]::NewGuid().ToString('N')
    $backup=Join-Path $target ('.hotfix-backup-'+$token)
    $incoming=Join-Path $target ('.hotfix-incoming-'+$token)
    New-Item -ItemType Directory -Path $backup,$incoming|Out-Null
    foreach($entry in $manifest.files){
        $staged=Join-Path $incoming $entry.path
        New-Item -ItemType Directory -Path (Split-Path $staged -Parent) -Force|Out-Null
        New-Item -ItemType Directory -Path (Split-Path (Join-Path $backup $entry.path) -Parent) -Force|Out-Null
        Copy-Item -LiteralPath (Join-Path $payload $entry.path) -Destination $staged
        if((Hash $staged) -ne $entry.after){throw 'Protected staging verification failed.'}
    }
    $swapped=[Collections.Generic.List[object]]::new()
    try {
        foreach($entry in $manifest.files){
            $destination=Join-Path $target $entry.path;$saved=Join-Path $backup $entry.path
            if((Hash $destination) -ne $entry.before){throw 'Installed file changed during preparation.'}
            # Preserve the old executable even if its image is still mapped.
            # Never terminate a transfer or process to apply the hotfix.
            [IO.File]::Move($destination,$saved);$swapped.Add($entry)
            [IO.File]::Move((Join-Path $incoming $entry.path),$destination)
            if((Hash $destination) -ne $entry.after){throw 'Installed replacement verification failed.'}
            if($FailAfterFirstSwap){throw 'Controlled private rollback fixture.'}
        }
    }catch{
        for($index=$swapped.Count-1;$index -ge 0;$index--){
            $entry=$swapped[$index];$destination=Join-Path $target $entry.path;$saved=Join-Path $backup $entry.path
            if(Test-Path -LiteralPath $destination){
                if((Hash $destination) -ne $entry.after){throw 'A changed replacement was preserved; manual rollback is required.'}
                [IO.File]::Move($destination,(Join-Path $incoming ($entry.path+'.rejected')))
            }
            [IO.File]::Move($saved,$destination)
        }
        throw
    }
    $report=@{ok=$true;product='Downshift';target=$target;backup=$backup;files=@($manifest.files);restartRequired=$restartRequired;processesTerminated=$false;registrationChanged=$false;userDataTouched=$false;isolated=[bool]$IsolatedFixture}
    $report|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $backup 'hotfix-receipt.json') -Encoding utf8
    $report|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $reportPath -Encoding utf8
    Write-Host 'Verified installed hotfix applied. Original files retained in the backup directory.'
}catch{
    @{ok=$false;error=$_.Exception.Message;isolated=[bool]$IsolatedFixture}|ConvertTo-Json|Set-Content -LiteralPath $reportPath -Encoding utf8
    Write-Error $_.Exception.Message
    exit 1
}
