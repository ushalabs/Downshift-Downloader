param([string]$OutputDirectory='', [switch]$RequireClean, [switch]$LocalPreview)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$sourceDirty=!!(& git -C $root status --porcelain)
if($RequireClean -and $sourceDirty){throw 'Release packaging requires a clean source checkout.'}
if($sourceDirty -and !$LocalPreview){throw 'Uncommitted packaging requires explicit -LocalPreview; release packaging still requires -RequireClean.'}
$fingerprint=Get-Content -LiteralPath (Join-Path $root 'build/windows-msvc/Release/build-source.json') -Raw -Encoding utf8|ConvertFrom-Json
if($fingerprint.configuration -ne 'Release'){throw 'Package requires a Release build fingerprint.'}
foreach($entry in $fingerprint.files.PSObject.Properties){if((Get-FileHash -LiteralPath (Join-Path $root $entry.Name) -Algorithm SHA256).Hash.ToLower() -ne $entry.Value){throw "Stale binary: rebuild before packaging ($($entry.Name))."}}
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'build\releases'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$build=Join-Path $root 'build\windows-msvc\Release'
$match=[regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw),'project\(Downshift VERSION ([0-9.]+)')
if(!$match.Success){throw 'Missing product version.'}
$version=$match.Groups[1].Value
$stage=Join-Path $OutputDirectory ('stage-'+[guid]::NewGuid().ToString('N'))
$name="Downshift-$version-windows-x64"
$portable=Join-Path $stage $name
New-Item -ItemType Directory -Force -Path $portable,$OutputDirectory | Out-Null
foreach($exe in @('Downshift.exe','DownshiftNativeHost.exe')){Copy-Item -LiteralPath (Join-Path $build $exe) -Destination $portable}
Get-ChildItem -LiteralPath $build -Filter '*.dll' -File | Copy-Item -Destination $portable
Copy-Item -LiteralPath (Join-Path $build 'platforms') -Destination $portable -Recurse
foreach($pluginDirectory in @('imageformats','tls')) { Copy-Item -LiteralPath (Join-Path $build $pluginDirectory) -Destination $portable -Recurse }
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'Visual Studio redist directory not found.'}
$redist=Get-ChildItem -LiteralPath (Join-Path $vs.Trim() 'VC\Redist\MSVC') -Directory | Where-Object Name -match '^\d+\.\d+\.\d+$' | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
$crt=Join-Path $redist.FullName 'x64\Microsoft.VC143.CRT'
foreach($dll in (Get-ChildItem -LiteralPath $crt -Filter '*.dll' -File)){
    if($dll.Length -lt 1000){throw "Invalid runtime DLL: $($dll.Name)"}
    Copy-Item -LiteralPath $dll.FullName -Destination $portable
}
Copy-Item -LiteralPath (Join-Path $root 'extension') -Destination $portable -Recurse
Copy-Item -Path (Join-Path $root 'packaging\*.cmd'),(Join-Path $root 'packaging\Read-me.txt') -Destination $portable
$scripts=Join-Path $portable 'scripts';New-Item -ItemType Directory -Path $scripts | Out-Null
foreach($script in @('Install-Portable.ps1','Uninstall-Portable.ps1','Install-MediaTools.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $script) -Destination $scripts}
Copy-Item -LiteralPath (Join-Path $root 'LICENSE'),(Join-Path $build 'build-source.json'),(Join-Path $root 'docs/TORRENTS.md') -Destination $portable
Copy-Item -LiteralPath (Join-Path $root 'docs\THIRD_PARTY_NOTICES.md') -Destination $portable
$licenses=Join-Path $portable 'licenses';New-Item -ItemType Directory -Path $licenses | Out-Null
$share=Join-Path $root 'vcpkg_installed\x64-windows\share'
$ports=Get-ChildItem -LiteralPath $share -Directory | Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'copyright')}
foreach($port in $ports){
    $notice=Join-Path $licenses $port.Name;New-Item -ItemType Directory -Path $notice | Out-Null
    Copy-Item -LiteralPath (Join-Path $port.FullName 'copyright') -Destination $notice
    Get-ChildItem -LiteralPath $port.FullName -Filter '*spdx*.json' -File | Copy-Item -Destination $notice
}
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$applicationZip=Join-Path $OutputDirectory "Downshift-$version-application-source.zip"
if(Test-Path -LiteralPath $applicationZip){Remove-Item -LiteralPath $applicationZip}
$stream=[IO.File]::Open($applicationZip,[IO.FileMode]::CreateNew)
$archive=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create)
$sourceFiles=@{}
try {
    $names=@(& git -C $root -c core.quotepath=false ls-files --cached --others --exclude-standard)|Sort-Object -Unique
    foreach($relative in $names){
        if($relative -match '^(build|vcpkg_installed|\.tools|\.downshift-data)/'){throw 'Private directory in source inventory.'}
        $path=[IO.Path]::GetFullPath((Join-Path $root $relative))
        if(!$path.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe source path.'}
        if(!(Test-Path -LiteralPath $path -PathType Leaf)){continue}
        $file=Get-Item -LiteralPath $path;if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked source file refused.'}
        $sourceFiles[$relative]=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower()
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$path,$relative,[IO.Compression.CompressionLevel]::Optimal)|Out-Null
    }
} finally {$archive.Dispose();$stream.Dispose()}
$sourceSha=(Get-FileHash -LiteralPath $applicationZip -Algorithm SHA256).Hash.ToLower()
@{localPreview=[bool]$LocalPreview;distributable=(!$sourceDirty -and !$LocalPreview);sourceSnapshot=$sourceFiles;sourceArchiveSha256=$sourceSha}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $portable 'source-snapshot.json') -Encoding utf8
$commit=(& git -C $root rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0){throw 'Cannot resolve source commit.'}
if($LocalPreview){'LOCAL VALIDATION PREVIEW — uncommitted source snapshot. Not a published or clean-source release. Do not distribute as a verified release.'|Set-Content -LiteralPath (Join-Path $portable 'LOCAL-PREVIEW.txt') -Encoding utf8}
$inventory=Get-ChildItem -LiteralPath $portable -File -Recurse | ForEach-Object {@{path=$_.FullName.Substring($portable.Length+1).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower();bytes=$_.Length}}
@{version=$version;sourceCommit=$commit;sourceDirty=$sourceDirty;localPreview=[bool]$LocalPreview;distributable=(!$sourceDirty -and !$LocalPreview);sourceArchiveSha256=$sourceSha;architecture='x64';configuration='Release';runtime='app-local VC143; dynamic Qt';optionalVideoHelpers='downloaded directly from upstream; not redistributed';files=@($inventory)}|ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $portable 'package-manifest.json') -Encoding utf8
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=Join-Path $OutputDirectory "$name.zip"
if(Test-Path -LiteralPath $zip){Remove-Item -LiteralPath $zip}
[IO.Compression.ZipFile]::CreateFromDirectory($portable,$zip,[IO.Compression.CompressionLevel]::Optimal,$true)
$extensionZip=Join-Path $OutputDirectory "Downshift-$version-chrome-extension.zip"
if(Test-Path -LiteralPath $extensionZip){Remove-Item -LiteralPath $extensionZip}
[IO.Compression.ZipFile]::CreateFromDirectory((Join-Path $root 'extension'),$extensionZip,[IO.Compression.CompressionLevel]::Optimal,$false)
    $vcpkg=Join-Path $root '.tools\windows\vcpkg'
    $source=Join-Path $stage 'dependency-sources';New-Item -ItemType Directory -Path $source | Out-Null
    $baseline=(Get-Content (Join-Path $root 'vcpkg.json') -Raw|ConvertFrom-Json).'builtin-baseline'
    if((& git -C $vcpkg rev-parse HEAD).Trim() -ne $baseline){throw 'vcpkg source baseline differs.'}
    $vcpkgArchive=Join-Path $source "vcpkg-$baseline.zip"
    & git -C $vcpkg archive --format=zip "--output=$vcpkgArchive" $baseline
    if($LASTEXITCODE -ne 0){throw 'vcpkg source archive failed.'}
    $resources=@();$downloads=Get-ChildItem -LiteralPath (Join-Path $vcpkg 'downloads') -File | Where-Object {$_.Name -notlike '*.part'}
    $hashes=@{}
    foreach($port in $ports){
        $resourceFile=Join-Path $port.FullName 'vcpkg-spdx-resources.json'
        if(!(Test-Path -LiteralPath $resourceFile)){continue}
        $packages=(Get-Content -LiteralPath $resourceFile -Raw|ConvertFrom-Json).packages
        foreach($package in $packages){
            $checksum=$package.checksums|Where-Object algorithm -eq 'SHA512'|Select-Object -First 1
            if(!$checksum){throw "Missing source checksum for $($port.Name)."}
            $expected=$checksum.checksumValue.ToLower();$found=$null
            foreach($file in $downloads){
                if(!$hashes.ContainsKey($file.FullName)){$hashes[$file.FullName]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA512).Hash.ToLower()}
                if($hashes[$file.FullName] -eq $expected){$found=$file;break}
            }
            if(!$found){
                $url=$package.downloadLocation
                if($url -match '^git\+https://github.com/([^@]+)@(.+)$'){$url="https://github.com/$($Matches[1])/archive/$($Matches[2]).tar.gz"}
                if($url -notmatch '^https://'){throw "Unsupported source location: $url"}
                $download=Join-Path $source ($expected.Substring(0,16)+$(if($url -like '*.xz'){'.tar.xz'}else{'.tar.gz'}))
                Invoke-WebRequest -Uri $url -OutFile $download
                if((Get-FileHash -LiteralPath $download -Algorithm SHA512).Hash.ToLower() -ne $expected){throw "Corresponding source checksum mismatch: $($port.Name)"}
                $found=Get-Item -LiteralPath $download
            }
            if($found.DirectoryName -ne $source){Copy-Item -LiteralPath $found.FullName -Destination $source -Force}
            $resources+=@{port=$port.Name;file=$found.Name;url=$package.downloadLocation;sha512=$expected}
        }
    }
    $resources|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $source 'sources.json') -Encoding utf8
    Copy-Item -LiteralPath $licenses -Destination $source -Recurse
    Copy-Item -LiteralPath (Join-Path $root 'vcpkg.json'),(Join-Path $root 'CMakePresets.json'),(Join-Path $root 'docs\REBUILD_LIBRARIES.md') -Destination $source
    $sourceZip=Join-Path $OutputDirectory "Downshift-$version-dependency-sources.zip"
    if(Test-Path -LiteralPath $sourceZip){Remove-Item -LiteralPath $sourceZip}
    [IO.Compression.ZipFile]::CreateFromDirectory($source,$sourceZip,[IO.Compression.CompressionLevel]::Optimal,$true)
Get-ChildItem -LiteralPath $OutputDirectory -Filter "Downshift-$version-*.zip" -File | Sort-Object Name | ForEach-Object {"$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower())  $($_.Name)"} | Set-Content -LiteralPath (Join-Path $OutputDirectory 'SHA256SUMS.txt') -Encoding ascii
Write-Host "Portable artifacts built at $OutputDirectory. Run Test-Portable.ps1 before distribution."
