param(
    [string]$PluginPath =
        "..\build\runtime-source-arbitration\Release\ds2_dll_music_resource.dll",
    [int]$Port = 47833,
    [int]$HoldMilliseconds = 0,
    [switch]$ServerOnly
)

$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class SourceArbitrationNative
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr LoadLibraryW(string path);

    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    public static extern IntPtr GetProcAddress(IntPtr module, string name);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool FreeLibrary(IntPtr module);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public delegate int ReadMetadata(
        StringBuilder title,
        UInt32 titleBytes,
        StringBuilder artist,
        UInt32 artistBytes);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate int ReadPlaybackState(
        out UInt32 version,
        out Int32 known,
        out Int32 paused);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public delegate int SendControl(string json);
}
"@

function Send-Text {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [string]$Text
    )
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    $segment = [System.ArraySegment[byte]]::new($bytes)
    $null = $Socket.SendAsync(
        $segment,
        [System.Net.WebSockets.WebSocketMessageType]::Text,
        $true,
        [Threading.CancellationToken]::None
    ).GetAwaiter().GetResult()
}

function Receive-Text {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket
    )
    $bytes = New-Object byte[] 4096
    $segment = [System.ArraySegment[byte]]::new($bytes)
    $cancel = [Threading.CancellationTokenSource]::new(3000)
    try {
        $result = $Socket.ReceiveAsync(
            $segment,
            $cancel.Token
        ).GetAwaiter().GetResult()
        return [System.Text.Encoding]::UTF8.GetString(
            $bytes,
            0,
            $result.Count
        )
    }
    finally {
        $cancel.Dispose()
    }
}

function Connect-Source {
    $socket = [System.Net.WebSockets.ClientWebSocket]::new()
    $null = $socket.ConnectAsync(
        [Uri]"ws://127.0.0.1:$Port",
        [Threading.CancellationToken]::None
    ).GetAwaiter().GetResult()
    return $socket
}

function Assert-Metadata {
    param(
        [SourceArbitrationNative+ReadMetadata]$Reader,
        [string]$ExpectedTitle,
        [string]$ExpectedArtist
    )
    $result = 0
    $title = $null
    $artist = $null
    $actualTitle = ""
    $actualArtist = ""
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        Start-Sleep -Milliseconds 50
        $title = [Text.StringBuilder]::new(256)
        $artist = [Text.StringBuilder]::new(256)
        $result = $Reader.Invoke($title, 256, $artist, 256)
        $actualTitle = $title.ToString()
        $actualArtist = $artist.ToString()
        if ($result -eq 1 -and
            [string]::Equals(
                $actualTitle,
                $ExpectedTitle,
                [StringComparison]::Ordinal
            ) -and
            [string]::Equals(
                $actualArtist,
                $ExpectedArtist,
                [StringComparison]::Ordinal
            )) {
            return
        }
    }
    throw "metadata mismatch result=$result " +
        "title='$actualTitle'($($actualTitle.Length)) " +
        "artist='$actualArtist'($($actualArtist.Length)) " +
        "expected='$ExpectedTitle'($($ExpectedTitle.Length)) / " +
        "'$ExpectedArtist'($($ExpectedArtist.Length))"
}

$resolvedPlugin = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath(
    $PluginPath
)
if (-not (Test-Path -LiteralPath $resolvedPlugin)) {
    throw "plugin not found: $resolvedPlugin"
}

$previousPort = [Environment]::GetEnvironmentVariable(
    "DS2_AUDIO_STREAM_PORT",
    [EnvironmentVariableTarget]::Process
)
[Environment]::SetEnvironmentVariable(
    "DS2_AUDIO_STREAM_PORT",
    $Port.ToString([Globalization.CultureInfo]::InvariantCulture),
    [EnvironmentVariableTarget]::Process
)
$module = [SourceArbitrationNative]::LoadLibraryW($resolvedPlugin)
if ($module -eq [IntPtr]::Zero) {
    [Environment]::SetEnvironmentVariable(
        "DS2_AUDIO_STREAM_PORT",
        $previousPort,
        [EnvironmentVariableTarget]::Process
    )
    throw "LoadLibrary failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
}

$tab = $null
$spotify = $null
try {
    Start-Sleep -Milliseconds 250
    if ($ServerOnly) {
        Write-Host "SERVER_ONLY_READY port=$Port"
        Start-Sleep -Milliseconds ([Math]::Max($HoldMilliseconds, 1000))
        return
    }
    $readAddress = [SourceArbitrationNative]::GetProcAddress(
        $module,
        "DS2AudioStreamReadMetadata"
    )
    $controlAddress = [SourceArbitrationNative]::GetProcAddress(
        $module,
        "DS2AudioStreamSendBrowserControl"
    )
    $stateAddress = [SourceArbitrationNative]::GetProcAddress(
        $module,
        "DS2AudioStreamReadPlaybackState"
    )
    if ($readAddress -eq [IntPtr]::Zero -or
        $controlAddress -eq [IntPtr]::Zero -or
        $stateAddress -eq [IntPtr]::Zero) {
        throw "required export missing"
    }
    $reader = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer(
        $readAddress,
        [SourceArbitrationNative+ReadMetadata]
    )
    $sendControl =
        [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer(
            $controlAddress,
            [SourceArbitrationNative+SendControl]
        )
    $readState =
        [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer(
            $stateAddress,
            [SourceArbitrationNative+ReadPlaybackState]
        )

    $spotify = Connect-Source
    Send-Text $spotify '{"type":"source_hello","sourceId":"spotify","sourceKind":"spotify_connect"}'
    Send-Text $spotify '{"type":"metadata","title":"Spotify Paused","artist":"Connect","paused":true}'
    Assert-Metadata $reader "[PAUSED] Spotify Paused" "Connect"
    [UInt32]$pausedVersion = 0
    [Int32]$known = 0
    [Int32]$paused = 0
    if ($readState.Invoke([ref]$pausedVersion, [ref]$known, [ref]$paused) -ne 1 -or
        $pausedVersion -eq 0 -or $known -ne 1 -or $paused -ne 1) {
        throw "paused playback state export mismatch"
    }

    if ($sendControl.Invoke(
        '{"type":"control","command":"resume","reason":"test_connected"}'
    ) -ne 1) {
        throw "connected source control send failed"
    }
    $connectedControl = Receive-Text $spotify
    if ($connectedControl -notlike '*"reason":"test_connected"*') {
        throw "control was not routed to connected source: $connectedControl"
    }
    Send-Text $spotify '{"type":"metadata","title":"Spotify Paused","artist":"Connect","paused":false}'
    Assert-Metadata $reader "Spotify Paused" "Connect"
    [UInt32]$playingVersion = 0
    if ($readState.Invoke([ref]$playingVersion, [ref]$known, [ref]$paused) -ne 1 -or
        $playingVersion -le $pausedVersion -or $known -ne 1 -or $paused -ne 0) {
        throw "playing playback state export mismatch"
    }
    Send-Text $spotify '{"type":"metadata","title":"Spotify Renamed","artist":"Connect","paused":false}'
    [UInt32]$renamedVersion = 0
    $readState.Invoke([ref]$renamedVersion, [ref]$known, [ref]$paused) | Out-Null
    if ($renamedVersion -ne $playingVersion) {
        throw "metadata-only change advanced playback state version"
    }

    $tab = Connect-Source
    Send-Text $tab '{"type":"source_hello","sourceId":"tab","sourceKind":"tab_capture"}'
    Send-Text $tab '{"type":"source_claim","sourceId":"tab","sourceKind":"tab_capture","reason":"test_tab"}'
    Send-Text $tab '{"type":"metadata","title":"Tab A","artist":"Browser"}'
    Assert-Metadata $reader "Tab A" "Browser"

    Send-Text $spotify '{"type":"metadata","title":"Ignored","artist":"Spotify"}'
    Assert-Metadata $reader "Tab A" "Browser"

    Send-Text $spotify '{"type":"source_claim","sourceId":"spotify","sourceKind":"spotify_connect","reason":"test_spotify"}'
    $tabPreempt = Receive-Text $tab
    if ($tabPreempt -notlike '*"reason":"source_preempted"*') {
        throw "tab did not receive preemption: $tabPreempt"
    }
    Send-Text $spotify '{"type":"metadata","title":"Spotify A","artist":"Connect"}'
    Assert-Metadata $reader "Spotify A" "Connect"

    Send-Text $tab '{"type":"metadata","title":"Ignored Tab","artist":"Browser"}'
    Assert-Metadata $reader "Spotify A" "Connect"

    Send-Text $tab '{"type":"source_claim","sourceId":"tab","sourceKind":"tab_capture","reason":"test_tab_return"}'
    $spotifyPreempt = Receive-Text $spotify
    if ($spotifyPreempt -notlike '*"reason":"source_preempted"*') {
        throw "spotify did not receive preemption: $spotifyPreempt"
    }
    Send-Text $tab '{"type":"metadata","title":"Tab B","artist":"Browser"}'
    Assert-Metadata $reader "Tab B" "Browser"

    if ($sendControl.Invoke(
        '{"type":"control","command":"pause","reason":"test_active"}'
    ) -ne 1) {
        throw "active control send failed"
    }
    $activeControl = Receive-Text $tab
    if ($activeControl -notlike '*"reason":"test_active"*') {
        throw "control was not routed to active source: $activeControl"
    }
    Write-Host "ARBITRATION_TEST_OK"
    if ($HoldMilliseconds -gt 0) {
        Start-Sleep -Milliseconds $HoldMilliseconds
    }
}
finally {
    if ($tab) {
        $tab.Abort()
        $tab.Dispose()
    }
    if ($spotify) {
        $spotify.Abort()
        $spotify.Dispose()
    }
    Start-Sleep -Milliseconds 100
    [SourceArbitrationNative]::FreeLibrary($module) | Out-Null
    [Environment]::SetEnvironmentVariable(
        "DS2_AUDIO_STREAM_PORT",
        $previousPort,
        [EnvironmentVariableTarget]::Process
    )
}
