param([Parameter(Mandatory=$true)][string]$PayloadDirectory,[Parameter(Mandatory=$true)][ValidatePattern('^[a-f0-9]{64}$')][string]$ManifestSha256,[string]$TargetDirectory='',[switch]$IsolatedFixture,[switch]$FailAfterFirstSwap)
$ErrorActionPreference='Stop'
function Hash([string]$path){$stream=[IO.File]::OpenRead($path);$hasher=[Security.Cryptography.SHA256]::Create();try{return ([BitConverter]::ToString($hasher.ComputeHash($stream))).Replace('-','').ToLower()}finally{$hasher.Dispose();$stream.Dispose()}}
function Ordinary([string]$path){if((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked update path refused: $path"}}
$payload=[IO.Path]::GetFullPath($PayloadDirectory)
$normal=[IO.Path]::GetFullPath((Join-Path $(if($env:ProgramW6432){$env:ProgramW6432}else{$env:ProgramFiles}) 'Downshift'))
if(!$TargetDirectory){$TargetDirectory=$normal}
$target=[IO.Path]::GetFullPath($TargetDirectory).TrimEnd('\')
$reportPath=Join-Path (Split-Path $payload -Parent) 'applied-report.json'
$changed=@();$entries=@();$backup=''
try{
    if($IsolatedFixture){$fixture=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../build/browser-fixes-20261104/install-fixture'));if(!$target.StartsWith($fixture+'\',[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath (Join-Path $target '.repair-fixture'))){throw 'Invalid isolated repair target.'}}
    else{if($target -ne $normal -or $FailAfterFirstSwap){throw 'Unexpected production target.'};$principal=[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent();if(!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Administrator permission is required to update Program Files.'}}
    Ordinary $target;Ordinary (Join-Path $target 'extension');Ordinary $payload;Ordinary (Join-Path $payload 'extension')
    if(@(Get-Process Downshift -ErrorAction SilentlyContinue|Where-Object {$_.Path -eq (Join-Path $target 'Downshift.exe')}).Count){throw 'Exit Downshift from its tray menu before applying this update. Active downloads were not stopped.'}
    $manifestFile=Join-Path $payload 'repair.json';Ordinary $manifestFile;if((Hash $manifestFile) -ne $ManifestSha256){throw 'Repair manifest hash differs.'}
    $manifest=Get-Content -LiteralPath $manifestFile -Raw -Encoding utf8|ConvertFrom-Json
    $names=@('Downshift.exe','build-source.json','extension/app-launcher.js','extension/background.js','extension/file-links.js','extension/handoff.js','extension/media.js','extension/video-button.js')
    $entries=@($manifest.files)
    if($manifest.schema -ne 1 -or $manifest.product -ne 'Downshift' -or $entries.Count -ne $names.Count -or ((@($entries|ForEach-Object {$_.path}|Sort-Object)-join '|') -ne (($names|Sort-Object)-join '|'))){throw 'Unexpected repair file set.'}
    foreach($entry in $entries){
        if($entry.after -notmatch '^[a-f0-9]{64}$' -or ($entry.before -and $entry.before -notmatch '^[a-f0-9]{64}$')){throw 'Invalid repair digest.'}
        if(!$entry.before -and $entry.path -ne 'extension/app-launcher.js'){throw 'Only the new launcher module may be absent.'}
        $old=Join-Path $target $entry.path;$new=Join-Path $payload $entry.path;Ordinary $new
        if((Hash $new) -ne $entry.after){throw "Repair payload differs: $($entry.path)"}
        if($entry.before){Ordinary $old;if((Hash $old) -ne $entry.before){throw "Installed file changed: $($entry.path)"}}elseif(Test-Path -LiteralPath $old){throw 'New launcher already exists with unreviewed content.'}
    }
    $token=[guid]::NewGuid().ToString('N');$backup=Join-Path $target ('.browser-repair-backup-'+$token);$incoming=Join-Path $target ('.browser-repair-incoming-'+$token)
    New-Item -ItemType Directory -Path $backup,$incoming|Out-Null
    foreach($entry in $entries){
        $staged=Join-Path $incoming $entry.path;New-Item -ItemType Directory -Force -Path (Split-Path $staged -Parent)|Out-Null;Copy-Item -LiteralPath (Join-Path $payload $entry.path) -Destination $staged
        if((Hash $staged) -ne $entry.after){throw 'Staged repair hash differs.'}
        if($entry.before){$saved=Join-Path $backup $entry.path;New-Item -ItemType Directory -Force -Path (Split-Path $saved -Parent)|Out-Null;Copy-Item -LiteralPath (Join-Path $target $entry.path) -Destination $saved;if((Hash $saved) -ne $entry.before){throw 'Backup hash differs.'}}
    }
    foreach($entry in $entries){
        $live=Join-Path $target $entry.path;$staged=Join-Path $incoming $entry.path
        if($entry.before){Ordinary $live;if((Hash $live) -ne $entry.before){throw 'Installed file changed during staging.'};$swapBackup=Join-Path $backup ('swapped/'+$entry.path);New-Item -ItemType Directory -Force -Path (Split-Path $swapBackup -Parent)|Out-Null;[IO.File]::Replace($staged,$live,$swapBackup)}else{[IO.File]::Move($staged,$live)}
        $changed+=,$entry;if((Hash $live) -ne $entry.after){throw 'Applied repair hash differs.'};if($FailAfterFirstSwap){throw 'Injected private rollback failure.'}
    }
    @{ok=$true;backup=$backup;files=@($entries.path);userDataChanged=$false;registryChanged=$false;extensionReloadRequired=$true}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $reportPath -Encoding utf8
    exit 0
}catch{
    $reason=$_.Exception.Message;$rollback=$true
    [array]::Reverse($changed)
    foreach($entry in $changed){
        try{$live=Join-Path $target $entry.path;Ordinary $live;if((Hash $live) -ne $entry.after){throw 'Changed file no longer owned by this repair.'};if($entry.before){Copy-Item -LiteralPath (Join-Path $backup $entry.path) -Destination $live -Force}else{Remove-Item -LiteralPath $live}}catch{$rollback=$false}
    }
    @{ok=$false;error=$reason;rollbackComplete=$rollback;backup=$backup}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $reportPath -Encoding utf8
    Write-Error $reason -ErrorAction Continue;exit 1
}
