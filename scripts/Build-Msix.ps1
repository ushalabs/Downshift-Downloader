param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$BinaryDirectory='',
    [string]$SdkDirectory='',
    [string]$IdentityName='UshaLabs.Downshift',
    [string]$Publisher='CN=UshaLabs',
    [string]$CertificateThumbprint='',
    [string]$StoreCandidatePublisher='',
    [ValidateRange(0,65535)][int]$LocalRevision=0,
    [string]$ChromeWebStoreUrl='',
    [switch]$UnsignedStore,
    [switch]$LocalPreview
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $output){throw 'Choose a new output directory for the MSIX and matching source archives.'}
if(!$BinaryDirectory){$BinaryDirectory=Join-Path $root 'build/windows-msvc/Release'}
if(!$SdkDirectory){
    $sdkBin=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $sdk=Get-ChildItem -LiteralPath $sdkBin -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' -and (Test-Path -LiteralPath (Join-Path $_.FullName 'x64/makeappx.exe')) } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if(!$sdk){throw 'Install the Windows SDK with MakeAppx and SignTool.'}
    $SdkDirectory=Join-Path $sdk.FullName 'x64'
}
if($UnsignedStore){
    if($LocalPreview -or $LocalRevision -or !$PSBoundParameters.ContainsKey('IdentityName') -or !$PSBoundParameters.ContainsKey('Publisher')){
        throw 'Store builds require clean source and the exact Package/Identity name and publisher from Partner Center.'
    }
    if($CertificateThumbprint){throw 'Do not pass a local signing certificate for an unsigned Store submission.'}
}elseif($CertificateThumbprint -notmatch '^[0-9a-fA-F]{40}$'){
    throw 'Pass a code-signing certificate thumbprint from Cert:\CurrentUser\My.'
}
if($ChromeWebStoreUrl -and $ChromeWebStoreUrl -cne 'https://chromewebstore.google.com/detail/empnpdkfnffpcgnfgdndkbielplcoioe'){
    throw 'Use the published Chrome Web Store URL for the existing Downshift extension ID.'
}
if($StoreCandidatePublisher -and ($UnsignedStore -or !$LocalPreview -or $StoreCandidatePublisher -notmatch '^CN=[0-9A-Fa-f-]{36}$')){
    throw 'For a local preview, pass the exact Partner Center publisher to produce a separate unsigned Store candidate.'
}

[xml]$manifest=Get-Content -LiteralPath (Join-Path $root 'packaging/AppxManifest.xml') -Raw
$manifest.Package.Identity.Name=$IdentityName
$manifest.Package.Identity.Publisher=$Publisher
$version=$manifest.Package.Identity.Version
if($version -ne '1.0.0.0'){throw 'This installer must have version 1.0.0.0.'}
if($LocalRevision){$manifest.Package.Identity.Version="1.0.0.$LocalRevision"}
$name='Downshift-1.0.0-windows-x64'
$workspace=Join-Path $output ('msix-stage-'+[guid]::NewGuid().ToString('N'))
$packageRoot=Join-Path $workspace 'package'
New-Item -ItemType Directory -Path $workspace,$packageRoot | Out-Null
try {
    $portableArtifacts=Join-Path $workspace 'portable'
    & (Join-Path $PSScriptRoot 'Build-Portable.ps1') -OutputDirectory $portableArtifacts -BinaryDirectory $BinaryDirectory -LocalPreview:$LocalPreview -RequireClean:(!$LocalPreview)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $portableArtifacts "$name.zip"),$workspace)
    $portable=Join-Path $workspace $name
    foreach($file in @('Downshift.exe','DownshiftNativeHost.exe','LICENSE','THIRD_PARTY_NOTICES.md','TORRENTS.md','build-source.json','source-snapshot.json')){
        Copy-Item -LiteralPath (Join-Path $portable $file) -Destination $packageRoot
    }
    Get-ChildItem -LiteralPath $portable -File -Filter '*.dll' | Copy-Item -Destination $packageRoot
    foreach($folder in @('platforms','imageformats','tls','licenses','extension','browser-extensions')){
        Copy-Item -LiteralPath (Join-Path $portable $folder) -Destination $packageRoot -Recurse
    }
    if($LocalPreview){Copy-Item -LiteralPath (Join-Path $portable 'LOCAL-PREVIEW.txt') -Destination $packageRoot}
    $scripts=Join-Path $packageRoot 'scripts'
    New-Item -ItemType Directory -Path $scripts | Out-Null
    foreach($script in @('media-tools-lock.json','Install-MediaTools.ps1','Setup-MsixChrome.ps1','Register-BrowserHosts.ps1','Browser-Paths.ps1')){
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $script) -Destination $scripts
    }
    @{chromeWebStoreUrl=$ChromeWebStoreUrl} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $packageRoot 'msix-setup.json') -Encoding utf8
    $manifest.Save((Join-Path $packageRoot 'AppxManifest.xml'))

    Add-Type -AssemblyName System.Drawing
    $assets=Join-Path $packageRoot 'StoreAssets'
    New-Item -ItemType Directory -Path $assets | Out-Null
    $image=[Drawing.Image]::FromFile((Join-Path $root 'assets/downshift.png'))
    try {
        function Write-Logo([string]$file,[int]$pixels){
            $bitmap=[Drawing.Bitmap]::new($pixels,$pixels)
            $graphics=[Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.DrawImage($image,0,0,$pixels,$pixels)
                $bitmap.Save((Join-Path $assets $file),[Drawing.Imaging.ImageFormat]::Png)
            } finally {$graphics.Dispose();$bitmap.Dispose()}
        }
        Write-Logo 'StoreLogo.png' 50
        Write-Logo 'Square150x150Logo.png' 150
        Write-Logo 'Square44x44Logo.png' 44
        foreach($pixels in @(16,20,24,30,32,36,40,48,60,64,72,80,96,256)){
            $target="Square44x44Logo.targetsize-$pixels"
            Write-Logo "$target.png" $pixels
            Copy-Item -LiteralPath (Join-Path $assets "$target.png") -Destination (Join-Path $assets "${target}_altform-unplated.png")
            Copy-Item -LiteralPath (Join-Path $assets "$target.png") -Destination (Join-Path $assets "${target}_altform-lightunplated.png")
        }
    } finally {$image.Dispose()}

    $fileName=if($UnsignedStore){"$name-store.msix"}else{"$name.msix"}
    $candidate=Join-Path $workspace $fileName
    & (Join-Path $SdkDirectory 'makeappx.exe') pack /d $packageRoot /p $candidate /h SHA256
    if($LASTEXITCODE -ne 0){throw 'MakeAppx could not create the package.'}
    if(!$UnsignedStore){
        $cert=Get-Item -LiteralPath "Cert:\CurrentUser\My\$CertificateThumbprint"
        if($cert.Subject -cne $Publisher -or !$cert.HasPrivateKey){throw 'The signing certificate must match the manifest publisher and have a private key.'}
        & (Join-Path $SdkDirectory 'signtool.exe') sign /fd SHA256 /sha1 $CertificateThumbprint /s My $candidate
        if($LASTEXITCODE -ne 0){throw 'MSIX signing failed.'}
        Export-Certificate -Cert $cert -FilePath (Join-Path $output 'Downshift-local-signer.cer') | Out-Null
    }
    $package=Join-Path $output $fileName
    Move-Item -LiteralPath $candidate -Destination $package
    if($StoreCandidatePublisher){
        $manifest.Package.Identity.Publisher=$StoreCandidatePublisher
        $manifest.Package.Identity.Version=$version
        $manifest.Save((Join-Path $packageRoot 'AppxManifest.xml'))
        $storeCandidate=Join-Path $workspace "$name-store-candidate.msix"
        & (Join-Path $SdkDirectory 'makeappx.exe') pack /d $packageRoot /p $storeCandidate /h SHA256
        if($LASTEXITCODE -ne 0){throw 'MakeAppx could not create the Store candidate.'}
        Move-Item -LiteralPath $storeCandidate -Destination $output
    }
    Get-ChildItem -LiteralPath $portableArtifacts -File -Filter '*.zip' | Move-Item -Destination $output
    Get-ChildItem -LiteralPath $output -File | Where-Object Extension -in @('.msix','.cer','.zip') |
        Sort-Object Name | ForEach-Object {"$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"} |
        Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ascii
    Write-Host "Created $package"
} finally {
    $resolved=[IO.Path]::GetFullPath($workspace)
    if(!$resolved.StartsWith($output+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Split-Path $resolved -Leaf) -notlike 'msix-stage-*'){throw 'Unsafe MSIX staging cleanup path.'}
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
