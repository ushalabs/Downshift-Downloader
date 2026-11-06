param([switch]$PrepareOnly,[switch]$OpenChrome)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$extension=Join-Path $root 'extension'
if(!(Test-Path -LiteralPath (Join-Path $extension 'manifest.json') -PathType Leaf)){throw 'The installed Chrome extension is missing.'}
$template=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'chrome-setup-template.txt') -Raw -Encoding utf8
$guide=Join-Path $root 'Chrome-setup.txt'
if($PrepareOnly){[IO.File]::WriteAllText($guide,$template.Replace('{{EXTENSION_PATH}}',$extension),[Text.UTF8Encoding]::new($true));return}
if(!(Test-Path -LiteralPath $guide -PathType Leaf)){throw 'Chrome-setup.txt is missing. Run setup again to restore the instructions.'}
if($OpenChrome){
    $chrome=@(
        (Join-Path $env:ProgramFiles 'Google\Chrome\Application\chrome.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Google\Chrome\Application\chrome.exe'),
        (Join-Path $env:LOCALAPPDATA 'Google\Chrome\Application\chrome.exe')
    ) | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | Select-Object -First 1
    if(!$chrome){throw 'Install Google Chrome, then choose Set up Chrome again.'}
    Start-Process -FilePath $chrome -ArgumentList 'chrome://extensions/' -WindowStyle Normal
    Write-Output 'DOWNSHIFT_UNPACKED_READY'
    return
}
# Only the user's explicit setup-finish/Start-menu action opens the text file.
Invoke-Item -LiteralPath $guide
