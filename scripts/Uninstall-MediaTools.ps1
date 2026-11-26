param()
$ErrorActionPreference='Stop'
function File-Sha256([string]$path){
    $inputStream=[IO.File]::OpenRead($path);$hasher=[Security.Cryptography.SHA256]::Create()
    try{return ([BitConverter]::ToString($hasher.ComputeHash($inputStream))).Replace('-','').ToLower()}
    finally{$hasher.Dispose();$inputStream.Dispose()}
}
$destination=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) 'media-tools'))
$receiptPath=Join-Path $destination 'installed-files.json'
if(!(Test-Path -LiteralPath $receiptPath -PathType Leaf)){return}
if((Get-Item -LiteralPath $destination).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked media-tools directory refused.'}
$receipt=Get-Content -LiteralPath $receiptPath -Raw|ConvertFrom-Json
$lockPath=Join-Path $PSScriptRoot 'media-tools-lock.json'
if($receipt.schema -notin @(1,2) -or $receipt.root -ne $destination -or $receipt.lockSha256 -ne (File-Sha256 $lockPath)){throw 'Video tool ownership differs; files preserved.'}
$lock=Get-Content -LiteralPath $lockPath -Raw|ConvertFrom-Json
if($receipt.schema -eq 2){
    if($receipt.extractor.sha256 -notmatch '^[a-f0-9]{64}$'){throw 'Invalid video extractor receipt.'}
    ($lock.files | Where-Object name -eq 'yt-dlp.exe').sha256=$receipt.extractor.sha256
}
foreach($file in $lock.files){
    $path=Join-Path $destination $file.name
    if((Test-Path -LiteralPath $path -PathType Leaf) -and (File-Sha256 $path) -eq $file.sha256){Remove-Item -LiteralPath $path -Force}
}
$sources=Join-Path $destination 'sources.json'
if((Test-Path -LiteralPath $sources -PathType Leaf) -and (File-Sha256 $sources) -eq $receipt.sourcesSha256){Remove-Item -LiteralPath $sources -Force}
Remove-Item -LiteralPath $receiptPath -Force
if(!(Get-ChildItem -LiteralPath $destination -Force | Select-Object -First 1)){Remove-Item -LiteralPath $destination -Force}
