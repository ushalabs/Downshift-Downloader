function Find-DownshiftBrowser {
    param([ValidateSet('Chrome','Edge','Firefox','Opera')][string]$Browser)
    $name=switch($Browser){'Chrome'{'chrome.exe'}'Edge'{'msedge.exe'}'Firefox'{'firefox.exe'}'Opera'{'opera.exe'}}
    foreach($hive in @([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryHive]::LocalMachine)){
        foreach($view in @([Microsoft.Win32.RegistryView]::Registry64,[Microsoft.Win32.RegistryView]::Registry32)){
            $registry=[Microsoft.Win32.RegistryKey]::OpenBaseKey($hive,$view)
            $key=$null
            try{
                $key=$registry.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\App Paths\'+$name)
                $candidate=if($key){[string]$key.GetValue('')}else{''}
                if($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)){return $candidate}
            }finally{if($key){$key.Dispose()};$registry.Dispose()}
        }
    }
    $paths=switch($Browser){
        'Chrome'{@('Google/Chrome/Application/chrome.exe')}
        'Edge'{@('Microsoft/Edge/Application/msedge.exe')}
        'Firefox'{@('Mozilla Firefox/firefox.exe','Firefox Developer Edition/firefox.exe','Programs/Mozilla Firefox/firefox.exe')}
        'Opera'{@('Programs/Opera/opera.exe','Programs/Opera/launcher.exe','Opera/opera.exe','Opera/launcher.exe','Programs/Opera GX/opera.exe','Programs/Opera GX/launcher.exe','Opera GX/opera.exe','Opera GX/launcher.exe')}
    }
    foreach($directory in @($env:ProgramFiles,${env:ProgramFiles(x86)},$env:LOCALAPPDATA)){
        foreach($relative in $paths){
            $candidate=Join-Path $directory $relative
            if(Test-Path -LiteralPath $candidate -PathType Leaf){return $candidate}
        }
    }
    return $null
}
