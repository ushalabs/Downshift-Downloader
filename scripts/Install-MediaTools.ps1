param([ValidateSet('Release','Debug')][string]$Configuration='Release',[string]$Destination='',[string]$CacheDirectory='',[switch]$RemoveDownloadCache)
$ErrorActionPreference='Stop'
function File-Sha256([string]$path){
    $inputStream=[IO.File]::OpenRead($path);$hasher=[Security.Cryptography.SHA256]::Create()
    try{return ([BitConverter]::ToString($hasher.ComputeHash($inputStream))).Replace('-','').ToLower()}
    finally{$hasher.Dispose();$inputStream.Dispose()}
}
$ProgressPreference='SilentlyContinue'
$projectRoot=Split-Path $PSScriptRoot -Parent
$portable=Test-Path -LiteralPath (Join-Path $projectRoot 'Downshift.exe')
if(!$CacheDirectory){$CacheDirectory=if($portable){Join-Path $env:LOCALAPPDATA 'Downshift\media-cache'}else{Join-Path $projectRoot '.tools\media'}}
if(!$Destination){$Destination=if($portable){Join-Path $projectRoot 'media-tools'}else{Join-Path $projectRoot "build\windows-msvc\$Configuration\media-tools"}}
$destination=[IO.Path]::GetFullPath($Destination)
$cache=[IO.Path]::GetFullPath($CacheDirectory)
$lock=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'media-tools-lock.json') -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Force -Path $destination | Out-Null
if((Get-Item -LiteralPath $destination).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked media-tools directory refused.'}
function Matches-Hash([string]$path,[string]$expected){return ((Test-Path -LiteralPath $path -PathType Leaf) -and (File-Sha256 $path) -eq $expected)}
$ready=$true
foreach($file in $lock.files){if(!(Matches-Hash (Join-Path $destination $file.name) $file.sha256)){$ready=$false;break}}
if(!$ready){
    New-Item -ItemType Directory -Force -Path $cache | Out-Null
    # Verify all archives before replacing an installed helper.
    foreach($asset in $lock.assets){
        $archive=Join-Path $cache $asset.name
        if(!(Matches-Hash $archive $asset.sha256)){
            Write-Host "Downloading $($asset.name) for video support..."
            $partial=Join-Path $cache ('download-'+[guid]::NewGuid().ToString('N')+'.tmp')
            try{
                Invoke-WebRequest -UseBasicParsing -Uri $asset.url -OutFile $partial -TimeoutSec 900
                if(!(Matches-Hash $partial $asset.sha256)){throw "Checksum mismatch: $($asset.name). Video tools were not installed."}
                Move-Item -LiteralPath $partial -Destination $archive -Force
            }finally{if(Test-Path -LiteralPath $partial){Remove-Item -LiteralPath $partial -Force}}
        }
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $stage=Join-Path $destination ('install-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    try{
        foreach($file in $lock.files){
            $output=Join-Path $stage $file.name
            if(!$file.entry){Copy-Item -LiteralPath (Join-Path $cache $file.asset) -Destination $output}
            else{
                $zip=[IO.Compression.ZipFile]::OpenRead((Join-Path $cache $file.asset))
                try{
                    $entry=$zip.GetEntry($file.entry);if(!$entry){throw "Missing video runtime: $($file.name)"}
                    [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$output)
                }finally{$zip.Dispose()}
            }
            if(!(Matches-Hash $output $file.sha256)){throw "Extracted video runtime differs: $($file.name)"}
        }
        foreach($file in $lock.files){Copy-Item -LiteralPath (Join-Path $stage $file.name) -Destination $destination -Force}
    }finally{
        $resolved=[IO.Path]::GetFullPath($stage)
        if(!$resolved.StartsWith($destination+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Split-Path $resolved -Leaf) -notlike 'install-*'){throw 'Unsafe video staging cleanup path.'}
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
foreach($file in $lock.files){if(!(Matches-Hash (Join-Path $destination $file.name) $file.sha256)){throw 'Video tools did not install completely.'}}
$lock.assets | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'sources.json') -Encoding utf8
@{schema=1;root=$destination;lockSha256=(File-Sha256 (Join-Path $PSScriptRoot 'media-tools-lock.json'));sourcesSha256=(File-Sha256 (Join-Path $destination 'sources.json'))}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $destination 'installed-files.json') -Encoding utf8
if($RemoveDownloadCache -and (Test-Path -LiteralPath $cache -PathType Container)){
    foreach($asset in $lock.assets){$path=Join-Path $cache $asset.name;if(Matches-Hash $path $asset.sha256){Remove-Item -LiteralPath $path -Force}}
    if(!(Get-ChildItem -LiteralPath $cache -Force | Select-Object -First 1)){Remove-Item -LiteralPath $cache -Force}
}
Write-Host 'Video support installed and verified. No system PATH or browser settings changed.'
