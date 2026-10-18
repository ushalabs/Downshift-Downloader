param([switch]$PrepareOnly)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$extension=Join-Path $root 'extension'
if(!(Test-Path -LiteralPath (Join-Path $extension 'manifest.json') -PathType Leaf)){throw 'The installed Chrome extension is missing.'}
$template=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'chrome-setup-template.txt') -Raw -Encoding utf8
$guide=Join-Path $root 'Chrome-setup.txt'
if($PrepareOnly){[IO.File]::WriteAllText($guide,$template.Replace('{{EXTENSION_PATH}}',$extension),[Text.UTF8Encoding]::new($true));return}
if(!(Test-Path -LiteralPath $guide -PathType Leaf)){throw 'Chrome-setup.txt is missing. Run setup again to restore the instructions.'}
# Only the user's explicit setup-finish/Start-menu action opens the text file.
Invoke-Item -LiteralPath $guide
