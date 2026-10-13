param([ValidateSet('Release','Debug')][string]$Configuration='Release',[string]$Destination='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$portable=Test-Path -LiteralPath (Join-Path $projectRoot 'Downshift.exe')
$cache=if($portable){Join-Path $env:LOCALAPPDATA 'Downshift\media-cache'}else{Join-Path $projectRoot '.tools\media'}
if(!$Destination){$Destination=if($portable){Join-Path $projectRoot 'media-tools'}else{Join-Path $projectRoot "build\windows-msvc\$Configuration\media-tools"}}
$destination=[System.IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $cache,$destination | Out-Null
$assets=@(
    @{Name='yt-dlp.exe';Url='https://github.com/yt-dlp/yt-dlp/releases/download/2026.08.19/yt-dlp.exe';Hash='66674953fe251b89f4d08c5f0e35e0728679bd67ab3d7d05c0562af101dd3e7a'},
    @{Name='deno.zip';Url='https://github.com/denoland/deno/releases/download/v2.9.7/deno-x86_64-pc-windows-msvc.zip';Hash='a0c3101b4158d1dfb7d6a78a7bf0f3de80c96bb423c152beec8beb22786f2238'},
    @{Name='ffmpeg.zip';Url='https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-10-12-13-12/ffmpeg-n8.1.3-18-g46d899d7a0-win64-lgpl-shared-8.1.zip';Hash='fa628b9de20254fe7c80605a4c6af54d0bffdda0807363ee7557131c2b78e051'}
)
foreach($asset in $assets) {
    $file=Join-Path $cache $asset.Name
    if(!(Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLower() -ne $asset.Hash) {
        Write-Host "Downloading $($asset.Name) from its pinned upstream release…"
        Invoke-WebRequest -Uri $asset.Url -OutFile $file
    }
    if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLower() -ne $asset.Hash) {throw "Checksum mismatch: $($asset.Name). No helper will be installed."}
}
Copy-Item -LiteralPath (Join-Path $cache 'yt-dlp.exe') -Destination $destination
Expand-Archive -LiteralPath (Join-Path $cache 'deno.zip') -DestinationPath (Join-Path $cache 'deno') -Force
Copy-Item -LiteralPath (Join-Path $cache 'deno\deno.exe') -Destination $destination
Expand-Archive -LiteralPath (Join-Path $cache 'ffmpeg.zip') -DestinationPath (Join-Path $cache 'ffmpeg') -Force
$ffmpegFolder=Get-ChildItem -LiteralPath (Join-Path $cache 'ffmpeg') -Directory | Select-Object -First 1
Copy-Item -Path (Join-Path $ffmpegFolder.FullName 'bin\*') -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $ffmpegFolder.FullName 'LICENSE.txt') -Destination (Join-Path $destination 'FFMPEG-LICENSE.txt')
$assets | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'sources.json') -Encoding utf8
Write-Host "Verified local video helpers installed in $destination. No system PATH or browser settings changed."
