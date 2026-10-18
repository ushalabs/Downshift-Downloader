param([Parameter(Mandatory=$true)][string]$PayloadDirectory)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$run=Join-Path $repo ('build/installed-hotfix-verification/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force|Out-Null
$script=Join-Path $repo 'scripts/Apply-InstalledHotfix.ps1'
$powerShell=(Get-Process -Id $PID).Path
$incoming=Get-Content -LiteralPath (Join-Path $PayloadDirectory 'hotfix.json') -Raw|ConvertFrom-Json
$hostExe=Join-Path $repo 'build/media-package/Release/DownshiftNativeHost.exe'
$checks=0
function Hash([string]$path){return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower()}
foreach($scenario in @('normal','rollback')){
    $root=Join-Path $run $scenario;$target=Join-Path $root 'installed';$payload=Join-Path $root 'payload'
    New-Item -ItemType Directory -Path $target,$payload -Force|Out-Null
    [IO.File]::WriteAllText((Join-Path $target '.hotfix-fixture'),'private fixture')
    [IO.File]::WriteAllText((Join-Path $target 'history.keep'),'private encrypted history sentinel')
    [IO.File]::WriteAllText((Join-Path $target 'settings.keep'),'private settings sentinel')
    $manifest=@{schema=1;product='Downshift';files=@()}
    foreach($entry in $incoming.files){
        $old=Join-Path $target $entry.path;$new=Join-Path $payload $entry.path
        New-Item -ItemType Directory -Path (Split-Path $old -Parent),(Split-Path $new -Parent) -Force|Out-Null
        Copy-Item -LiteralPath (Join-Path $PayloadDirectory $entry.path) -Destination $new
        if($entry.path -eq 'Downshift.exe'){Copy-Item -LiteralPath $hostExe -Destination $old}else{[IO.File]::WriteAllText($old,'Original private fixture content')}
        $manifest.files+=@{path=$entry.path;before=(Hash $old);after=(Hash $new)}
    }
    $manifest|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $payload 'hotfix.json') -Encoding utf8
    $manifestHash=Hash (Join-Path $payload 'hotfix.json')
    $mapped=$null
    try{
        if($scenario -eq 'normal'){
            Get-ChildItem -LiteralPath (Split-Path $hostExe -Parent) -Filter '*.dll' -File|Copy-Item -Destination $target
            $start=[Diagnostics.ProcessStartInfo]::new((Join-Path $target 'Downshift.exe'),'chrome-extension://empnpdkfnffpcgnfgdndkbielplcoioe/')
            $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardInput=$true
            $mapped=[Diagnostics.Process]::Start($start)
            Start-Sleep -Milliseconds 150
            if($mapped.HasExited){throw 'Private mapped image fixture exited before replacement.'}
        }
        $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$script,'-PayloadDirectory',$payload,'-ManifestSha256',$manifestHash,'-TargetDirectory',$target,'-IsolatedFixture')
        if($scenario -eq 'rollback'){$arguments+='-FailAfterFirstSwap'}
        & $powerShell @arguments *> (Join-Path $root 'apply.log');$code=$LASTEXITCODE
        if(($scenario -eq 'normal' -and $code -ne 0) -or ($scenario -eq 'rollback' -and $code -eq 0)){Get-Content -LiteralPath (Join-Path $root 'apply.log');throw 'Private updater result differs.'};$checks++
        foreach($entry in $manifest.files){$expected=if($scenario -eq 'normal'){$entry.after}else{$entry.before};if((Hash (Join-Path $target $entry.path)) -ne $expected){throw 'Installed/rollback file hash differs.'};$checks++}
        foreach($name in @('history.keep','settings.keep')){if(!(Test-Path -LiteralPath (Join-Path $target $name))){throw 'Private user sentinel removed.'};$checks++}
        if($mapped){if($mapped.HasExited){throw 'Updater stopped the mapped image.'};$checks++;$mapped.StandardInput.Close();if(!$mapped.WaitForExit(5000) -or $mapped.ExitCode -ne 0){throw 'Private mapped host did not exit cleanly.'};$checks++}
    }finally{if($mapped){if(!$mapped.HasExited){$mapped.Kill();$mapped.WaitForExit()};$mapped.Dispose()}}
}
@{checks=$checks;scenarios=@('mapped image replacement without termination','controlled rollback');normalInstallationChanged=$false;fixtureRoot=$run}|ConvertTo-Json -Depth 3|Set-Content -LiteralPath (Join-Path $repo 'build/installed-youtube-hotfix/updater-test-report.json') -Encoding utf8
Write-Host "Installed hotfix checks passed: $checks"
