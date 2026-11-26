# Official stable yt-dlp discovery. The selected tag, size and upstream checksum
# are fixed for the duration of an installation, even if a new release appears.
function Get-ExtractorChecksum([string]$text) {
    $matchesFound=@([regex]::Matches($text,'(?m)^([a-fA-F0-9]{64}) [ *]yt-dlp\.exe\r?$'))
    if($matchesFound.Count -ne 1){throw 'The official yt-dlp checksum list is missing or ambiguous.'}
    return $matchesFound[0].Groups[1].Value.ToLowerInvariant()
}
function Get-LatestVideoExtractor {
    Add-Type -AssemblyName System.Net.Http
    $client=[Net.Http.HttpClient]::new();$client.Timeout=[TimeSpan]::FromSeconds(40)
    $client.DefaultRequestHeaders.UserAgent.ParseAdd('Downshift/1.0.0')
    try{
        $response=$client.GetAsync('https://github.com/yt-dlp/yt-dlp/releases/latest',[Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
        try{
            $null=$response.EnsureSuccessStatusCode();$uri=$response.RequestMessage.RequestUri
            if($uri.Scheme -ne 'https' -or $uri.Host -ne 'github.com' -or $uri.AbsolutePath -notmatch '^/yt-dlp/yt-dlp/releases/tag/(\d{4}\.\d{2}\.\d{2}(?:\.\d+)?)$'){throw 'Unexpected official yt-dlp release location.'}
            $version=$Matches[1]
        }finally{$response.Dispose()}
        $base="https://github.com/yt-dlp/yt-dlp/releases/download/$version"
        $response=$client.GetAsync("$base/SHA2-256SUMS",[Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
        try{
            $null=$response.EnsureSuccessStatusCode()
            if($response.Content.Headers.ContentLength -gt 131072){throw 'The upstream checksum list is too large.'}
            $stream=$response.Content.ReadAsStreamAsync().GetAwaiter().GetResult();$data=[IO.MemoryStream]::new()
            try{
                $buffer=[byte[]]::new(4096)
                while(($count=$stream.Read($buffer,0,$buffer.Length)) -gt 0){if($data.Length+$count -gt 131072){throw 'The upstream checksum list is too large.'};$data.Write($buffer,0,$count)}
                $digest=Get-ExtractorChecksum ([Text.Encoding]::UTF8.GetString($data.ToArray()))
            }finally{$data.Dispose();$stream.Dispose()}
        }finally{$response.Dispose()}
        $request=[Net.Http.HttpRequestMessage]::new([Net.Http.HttpMethod]::Head,"$base/yt-dlp.exe")
        try{
            $response=$client.SendAsync($request,[Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
            try{$null=$response.EnsureSuccessStatusCode();$size=$response.Content.Headers.ContentLength;if(!$size -or $size -lt 1000000 -or $size -gt 134217728){throw 'Invalid official yt-dlp executable size.'}}
            finally{$response.Dispose()}
        }finally{$request.Dispose()}
        return [pscustomobject]@{version=$version;url="$base/yt-dlp.exe";sha256=$digest;size=[long]$size}
    }finally{$client.Dispose()}
}
