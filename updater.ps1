# The app supplies its PID and installed version before this embedded script.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName Microsoft.VisualBasic
function Notice($message, $style) {
    [Microsoft.VisualBasic.Interaction]::MsgBox($message, $style -bor 65536, 'PadClip updates')
}

function LatestRelease($client, $root) {
    $text = $client.GetStringAsync("$root/update.json").GetAwaiter().GetResult()
    $release = $text | ConvertFrom-Json
    if ($release.version -notmatch '^\d+\.\d+\.\d+$' -or
        $release.sha256 -notmatch '^[a-fA-F0-9]{64}$') {
        throw 'The update information is incomplete. Please check again later.'
    }
    return $release
}

function StageUpdate($client, $root, $release, $folder) {
    Add-Type -AssemblyName System.IO.Compression
    $bytes = $client.GetByteArrayAsync("$root/8BitTrade-PadClip-Windows.zip").GetAwaiter().GetResult()
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $digest = [BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-', '')
    } finally { $sha.Dispose() }
    if ($digest -ne $release.sha256) {
        throw 'The download did not match the published update. Please check again.'
    }
    $stream = [IO.MemoryStream]::new($bytes, $false)
    try {
        $zip = [IO.Compression.ZipArchive]::new($stream)
        try {
            $entry = $zip.GetEntry('PadClip.exe')
            if (!$entry -or $entry.Length -le 0 -or $entry.Length -gt 32MB) {
                throw 'The update package is missing a valid PadClip executable.'
            }
            $source = $entry.Open()
            try {
                $file = [IO.File]::Open((Join-Path $folder 'new.exe'), 'CreateNew', 'Write', 'None')
                try { $source.CopyTo($file); $file.Flush($true) } finally { $file.Dispose() }
            } finally { $source.Dispose() }
        } finally { $zip.Dispose() }
    } finally { $stream.Dispose() }
}

function InstallUpdate($folder, $exe) {
    $new = Join-Path $folder 'new.exe'
    $backup = Join-Path $folder 'previous.exe'
    [IO.File]::Replace($new, $exe, $backup)
    try {
        [void][Diagnostics.Process]::Start($exe, '--updated')
    } catch {
        [IO.File]::Replace($backup, $exe, $new)
        throw
    }
}

$client = $parent = $ready = $null
$folder = $null
$handedOff = $false
$installed = $false
try {
    $parent = [Diagnostics.Process]::GetProcessById($ownerPid)
    [void]$parent.Handle
    $exe = $parent.MainModule.FileName
    $ready = [Threading.EventWaitHandle]::OpenExisting("Local\8BitTrade.PadClip.Update.$ownerPid")
    Add-Type -AssemblyName System.Net.Http
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $handler = [Net.Http.HttpClientHandler]::new()
    $handler.AllowAutoRedirect = $false
    $client = [Net.Http.HttpClient]::new($handler)
    $client.Timeout = [TimeSpan]::FromSeconds(45)
    $client.MaxResponseContentBufferSize = 32MB
    $client.DefaultRequestHeaders.CacheControl = [Net.Http.Headers.CacheControlHeaderValue]::new()
    $client.DefaultRequestHeaders.CacheControl.NoCache = $true
    $root = 'https://raw.githubusercontent.com/BabyCRTTV/PadClip/main'
    $release = LatestRelease $client $root
    if ($parent.HasExited) { return }
    if ([version]$release.version -le $installedVersion) {
        [void](Notice "You're up to date. You're running PadClip $installedVersion." 64)
        return
    }
    $answer = Notice "PadClip $($release.version) is available. You're running $installedVersion.`r`n`r`nInstall the update and restart PadClip now?" (4 -bor 32 -bor 256)
    if ($answer -ne 'Yes' -or $parent.HasExited) { return }

    $folder = Join-Path ([IO.Path]::GetDirectoryName($exe)) ('.padclip-' + [guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($folder)
    StageUpdate $client $root $release $folder
    if ($parent.HasExited) { return }
    [void]$ready.Set()
    if (!$parent.WaitForExit(15000)) {
        [void]$ready.Reset()
        throw 'PadClip could not close for the update.'
    }
    $handedOff = $true
    InstallUpdate $folder $exe
    $installed = $true
} catch {
    [void](Notice ("PadClip could not complete the update.`r`n`r`n" + $_.Exception.GetBaseException().Message) 48)
    if ($handedOff -and [IO.File]::Exists($exe)) {
        [void][Diagnostics.Process]::Start($exe)
    }
} finally {
    if ($folder -and [IO.Directory]::Exists($folder)) {
        # Keep the backup recoverable if Windows itself could not restore it.
        if ([IO.File]::Exists((Join-Path $folder 'previous.exe')) -and
            !$installed) {
            [void](Notice "Your previous PadClip executable is in $folder." 48)
        } else {
            Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    if ($ready) { $ready.Dispose() }
    if ($client) { $client.Dispose() }
    if ($parent) { $parent.Dispose() }
}
