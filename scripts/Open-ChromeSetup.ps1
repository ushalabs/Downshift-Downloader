param([switch]$PrepareOnly,[switch]$OpenChrome,[ValidateSet('Chrome','Edge','Firefox','Opera')][string]$Browser='Chrome')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Browser-Paths.ps1')
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$extension=Join-Path $root 'extension'
if($Browser -ne 'Chrome'){$extension=Join-Path $root ('browser-extensions/'+$Browser.ToLowerInvariant())}
if(!(Test-Path -LiteralPath (Join-Path $extension 'manifest.json') -PathType Leaf)){throw 'The installed Chrome extension is missing.'}
$template=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'chrome-setup-template.txt') -Raw -Encoding utf8
$guide=Join-Path $root 'Chrome-setup.txt'
if($PrepareOnly){[IO.File]::WriteAllText($guide,$template.Replace('{{EXTENSION_PATH}}',$extension),[Text.UTF8Encoding]::new($true));return}
if(!(Test-Path -LiteralPath $guide -PathType Leaf)){throw 'Chrome-setup.txt is missing. Run setup again to restore the instructions.'}
if($OpenChrome){
    $chrome=Find-DownshiftBrowser $Browser
    $url='chrome://extensions/'
    if($Browser -ne 'Chrome'){
        $url=switch($Browser){'Edge'{'edge://extensions/'}'Firefox'{'about:debugging#/runtime/this-firefox'}'Opera'{'opera://extensions/'}}
    }
    if(!$chrome){throw "Install $Browser, then choose browser setup again."}
    Start-Process -FilePath $chrome -ArgumentList $url -WindowStyle Normal
    Write-Output 'DOWNSHIFT_UNPACKED_READY'
    return
}
# Only the user's explicit setup-finish/Start-menu action opens the text file.
Invoke-Item -LiteralPath $guide
