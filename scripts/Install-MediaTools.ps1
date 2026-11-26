param([ValidateSet('Release','Debug')][string]$Configuration='Release',[string]$Destination='',[string]$CacheDirectory='',[switch]$RemoveDownloadCache,[switch]$RefreshExtractorOnly)
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
. (Join-Path $PSScriptRoot 'Video-Extractor.ps1')
New-Item -ItemType Directory -Force -Path $destination | Out-Null
if((Get-Item -LiteralPath $destination).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked media-tools directory refused.'}
$installGuard=$null;$guardDeadline=[DateTime]::UtcNow.AddSeconds(25)
while(!$installGuard){try{$installGuard=[IO.File]::Open((Join-Path $destination '.downshift-installer.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}catch{if([DateTime]::UtcNow -gt $guardDeadline){throw 'Video support is being updated by another Downshift process. Retry shortly.'};Start-Sleep -Milliseconds 250}}
try {
$receiptPath=Join-Path $destination 'installed-files.json'
$previous=if(Test-Path -LiteralPath $receiptPath -PathType Leaf){Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json}else{$null}
$extractor=Join-Path $destination 'yt-dlp.exe'
$installedHash=if(Test-Path -LiteralPath $extractor -PathType Leaf){File-Sha256 $extractor}else{''}
$knownHash=if($previous.schema -eq 2){[string]$previous.extractor.sha256}else{[string]($lock.files | Where-Object name -eq 'yt-dlp.exe').sha256}
$knownGood=$installedHash -and $installedHash -ceq $knownHash
if($RefreshExtractorOnly -and $knownGood -and $previous.schema -eq 2){
    $checked=[DateTimeOffset]::MinValue
    $validTime=if($previous.extractor.checkedAt -is [DateTime]){$checked=[DateTimeOffset]$previous.extractor.checkedAt;$true}else{[DateTimeOffset]::TryParse([string]$previous.extractor.checkedAt,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::AssumeUniversal,[ref]$checked)}
    if($validTime -and $checked -le [DateTimeOffset]::UtcNow -and $checked -gt [DateTimeOffset]::UtcNow.AddHours(-24)){Write-Host 'Video extractor is current (verified within the last day).';return}
}
Write-Host 'Downshift setup: Checking the latest video extractor...'
try{$latest=Get-LatestVideoExtractor}
catch{
    # An offline update check must not disable an already verified installation.
    if($RefreshExtractorOnly -and $knownGood){Write-Host 'The update check is unavailable. Keeping the verified installed extractor.';return}
    throw
}
($lock.assets | Where-Object name -eq 'yt-dlp.exe').url=$latest.url
($lock.assets | Where-Object name -eq 'yt-dlp.exe').sha256=$latest.sha256
($lock.files | Where-Object name -eq 'yt-dlp.exe').sha256=$latest.sha256
if($RefreshExtractorOnly){$lock.assets=@($lock.assets | Where-Object name -eq 'yt-dlp.exe');$lock.files=@($lock.files | Where-Object name -eq 'yt-dlp.exe')}
# Keep size metadata outside the lock so existing verified install receipts remain valid.
$archiveSizes=@{
    '66674953fe251b89f4d08c5f0e35e0728679bd67ab3d7d05c0562af101dd3e7a'=17840399L
    'a0c3101b4158d1dfb7d6a78a7bf0f3de80c96bb423c152beec8beb22786f2238'=42630221L
    '5cef7bc48e513f3cee7c881e8b2999d835541edb1846c3e89443a67f8f8556ab'=80356594L
}
$archiveSizes[$latest.sha256]=$latest.size
foreach($asset in $lock.assets){
    if(!$archiveSizes.ContainsKey($asset.sha256)){throw "Missing pinned archive size: $($asset.name)"}
}
$total=[long]($lock.assets | ForEach-Object {$archiveSizes[$_.sha256]} | Measure-Object -Sum).Sum
$received=0L
function Write-DownloadProgress([long]$bytes){
    [Console]::WriteLine("Downshift progress: $bytes $total")
}
function Receive-Archive($asset,[string]$url,[string]$partial,[long]$alreadyReceived){
    Add-Type -AssemblyName System.Net.Http
    $client=[Net.Http.HttpClient]::new()
    $client.Timeout=[Threading.Timeout]::InfiniteTimeSpan
    $deadline=[Threading.CancellationTokenSource]::new([TimeSpan]::FromMinutes(15))
    $expectedSize=$archiveSizes[$asset.sha256]
    try{
        $response=$client.GetAsync($url,[Net.Http.HttpCompletionOption]::ResponseHeadersRead,$deadline.Token).GetAwaiter().GetResult()
        try{
            $null=$response.EnsureSuccessStatusCode()
            $inputStream=$response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
            $output=[IO.File]::Open($partial,[IO.FileMode]::CreateNew)
            try{
                $buffer=[byte[]]::new(65536)
                $bytes=0L
                $clock=[Diagnostics.Stopwatch]::StartNew()
                while(($count=$inputStream.ReadAsync($buffer,0,$buffer.Length,$deadline.Token).GetAwaiter().GetResult()) -gt 0){
                    $bytes+=$count
                    if($bytes -gt $expectedSize){throw 'The archive exceeds its pinned size.'}
                    $output.Write($buffer,0,$count)
                    if($clock.ElapsedMilliseconds -ge 200){Write-DownloadProgress ($alreadyReceived+$bytes);$clock.Restart()}
                }
                if($bytes -ne $expectedSize){throw 'The archive did not reach its pinned size.'}
                Write-DownloadProgress ($alreadyReceived+$bytes)
            }finally{$output.Dispose()}
        }finally{$response.Dispose()}
    }finally{$deadline.Dispose();$client.Dispose()}
}
Write-DownloadProgress 0
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
            Write-Host "Downshift setup: Downloading $($asset.name)..."
            $partial=Join-Path $cache ('download-'+[guid]::NewGuid().ToString('N')+'.tmp')
            try{
                try{Receive-Archive $asset $asset.url $partial $received}
                catch{
                    $url=[uri]$asset.url
                    if($url.Host -ne 'github.com' -or $url.Query -or !$url.AbsolutePath.Contains('/releases/download/')){throw}
                    if(Test-Path -LiteralPath $partial){Remove-Item -LiteralPath $partial -Force}
                    Write-DownloadProgress $received
                    Write-Host "Downshift setup: Retrying $($asset.name)..."
                    Receive-Archive $asset ($asset.url+'?download=1') $partial $received
                }
                Write-Host "Downshift setup: Verifying $($asset.name)..."
                if(!(Matches-Hash $partial $asset.sha256)){throw "Checksum mismatch: $($asset.name). Video tools were not installed."}
                Move-Item -LiteralPath $partial -Destination $archive -Force
            }finally{if(Test-Path -LiteralPath $partial){Remove-Item -LiteralPath $partial -Force}}
        }
        $received+=$archiveSizes[$asset.sha256]
        Write-DownloadProgress $received
    }
    Write-Host "Downshift setup: Extracting and verifying video tools..."
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
        foreach($file in $lock.files){Move-Item -LiteralPath (Join-Path $stage $file.name) -Destination (Join-Path $destination $file.name) -Force}
    }finally{
        $resolved=[IO.Path]::GetFullPath($stage)
        if(!$resolved.StartsWith($destination+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Split-Path $resolved -Leaf) -notlike 'install-*'){throw 'Unsafe video staging cleanup path.'}
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
Write-DownloadProgress $total
foreach($file in $lock.files){if(!(Matches-Hash (Join-Path $destination $file.name) $file.sha256)){throw 'Video tools did not install completely.'}}
if($RemoveDownloadCache -and (Test-Path -LiteralPath $cache -PathType Container)){
    foreach($asset in $lock.assets){$path=Join-Path $cache $asset.name;if(Matches-Hash $path $asset.sha256){Remove-Item -LiteralPath $path -Force}}
    if(!(Get-ChildItem -LiteralPath $cache -Force | Select-Object -First 1)){Remove-Item -LiteralPath $cache -Force}
}
$receiptFiles=if($RefreshExtractorOnly){$full=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'media-tools-lock.json') -Raw | ConvertFrom-Json;($full.files | Where-Object name -eq 'yt-dlp.exe').sha256=$latest.sha256;@($full.files)}else{@($lock.files)}
$receiptAssets=if($RefreshExtractorOnly){$full.assets=@($full.assets | ForEach-Object {if($_.name -eq 'yt-dlp.exe'){($lock.assets | Select-Object -First 1)}else{$_}});@($full.assets)}else{@($lock.assets)}
$receiptAssets | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destination 'sources.json') -Encoding utf8
$receipt=@{schema=2;root=$destination;lockSha256=(File-Sha256 (Join-Path $PSScriptRoot 'media-tools-lock.json'));sourcesSha256=(File-Sha256 (Join-Path $destination 'sources.json'));files=$receiptFiles;extractor=@{version=$latest.version;sha256=$latest.sha256;checkedAt=[DateTimeOffset]::UtcNow.ToString('o')}}
$receiptTemporary=Join-Path $destination ('receipt-'+[guid]::NewGuid().ToString('N')+'.tmp')
try{$receipt|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $receiptTemporary -Encoding utf8;Move-Item -LiteralPath $receiptTemporary -Destination $receiptPath -Force}finally{if(Test-Path -LiteralPath $receiptTemporary){Remove-Item -LiteralPath $receiptTemporary -Force}}
Write-Host 'Video support installed and verified. No system PATH or browser settings changed.'
}finally{$installGuard.Dispose()}
