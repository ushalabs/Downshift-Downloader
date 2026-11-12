param([string]$OutputDirectory='', [switch]$FoldersOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'dist/browser-extensions'}
$output=[IO.Path]::GetFullPath($OutputDirectory)
$source=Join-Path $root 'extension'
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach($target in @('edge','firefox','opera')){
    $folder=Join-Path $output $target
    if(Test-Path -LiteralPath $folder){throw "Choose a new extension output directory. Already exists: $folder"}
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
    Get-ChildItem -LiteralPath $source | Where-Object Name -ne 'package.json' | Copy-Item -Destination $folder -Recurse
    $manifest=Get-Content -LiteralPath (Join-Path $source 'manifest.json') -Raw | ConvertFrom-Json
    if($target -eq 'firefox'){
        $manifest.PSObject.Properties.Remove('key')
        $manifest.PSObject.Properties.Remove('minimum_chrome_version')
        $manifest.permissions=@($manifest.permissions | Where-Object {$_ -ne 'downloads.ui'})
        $manifest.background=[pscustomobject]@{scripts=@('background.js');type='module'}
        $manifest | Add-Member -NotePropertyName incognito -NotePropertyValue 'not_allowed'
        $manifest | Add-Member -NotePropertyName browser_specific_settings -NotePropertyValue @{
            gecko=@{id='downshift@ushalabs';strict_min_version='140.0';data_collection_permissions=@{required=@('browsingActivity','websiteContent','websiteActivity')}}
        }
        $instructions=@'
Downshift for Mozilla Firefox

Install the updated Downshift Windows application before connecting this add-on.
For a local test, open about:debugging#/runtime/this-firefox, click Load Temporary
Add-on and choose manifest.json from this folder. Grant website access when asked.
The temporary add-on is removed when Firefox closes. Permanent installation in
standard Firefox requires Mozilla signing through addons.mozilla.org.

Firefox asks permission to send download links, selected media metadata and
download actions to the Downshift application on this computer. Read PRIVACY.md.
Private windows are excluded from the Firefox add-on.

Enable Use Downshift for downloads in the extension popup. Open a video page to
use Download this video. Generated and signed-in files retain their browser
response until Downshift verifies the saved file. Firefox controls its own
download panel because it does not provide Chromium's download UI switch.
'@
    }else{
        $page=if($target -eq 'edge'){'edge://extensions/'}else{'opera://extensions/'}
        $instructions="Downshift for $target`r`n`r`nInstall the updated Downshift Windows application first.`r`n1. Open $page`r`n2. Turn on Developer mode and click Load unpacked.`r`n3. Select this folder.`r`n4. Pin Downshift and leave Use Downshift for downloads enabled.`r`n`r`nDownload files normally or use Download this video on a video player.`r`n"
    }
    [IO.File]::WriteAllText((Join-Path $folder 'manifest.json'),($manifest | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $folder 'INSTALL.txt'),$instructions,[Text.UTF8Encoding]::new($false))
    if(!$FoldersOnly){
        $zip=Join-Path $output "Downshift-$($manifest.version)-$target-extension.zip"
        if(Test-Path -LiteralPath $zip){throw "An extension archive already exists: $zip"}
        [IO.Compression.ZipFile]::CreateFromDirectory($folder,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)
        if($target -eq 'firefox'){Copy-Item -LiteralPath $zip -Destination ([IO.Path]::ChangeExtension($zip,'.xpi'))}
    }
}
Write-Host "Edge, Firefox and Opera extensions built in $output"
