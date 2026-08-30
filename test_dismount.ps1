param(
    [int]$MouseStepX = -1,
    [int]$MouseStepY = 0,
    [int]$MouseIntervalMs = 10,
    [int]$MouseDurationMs = 1200
)

$ErrorActionPreference = "Stop"

$gameDir = "F:\SteamLibrary\steamapps\common\DEATH STRANDING 2 - ON THE BEACH"
$logPath = Join-Path $gameDir "log.txt"
$inputSource = Join-Path $PSScriptRoot "tools\BoardingTestInput.cs"
$mouseInputSource = Join-Path $PSScriptRoot "tools\BoardingTestMouseCapture.cs"
try {
    Add-Type -AssemblyName System.Drawing.Common -ErrorAction Stop
}
catch {
    Add-Type -AssemblyName System.Drawing
}
Add-Type -Path @($inputSource, $mouseInputSource) -ReferencedAssemblies (
    [System.Drawing.Bitmap].Assembly.Location)
$SI = [BoardingTestInput]
$script:gamePid = 0
$script:gameHwnd = [IntPtr]::Zero

function Get-LogLines {
    if (!(Test-Path -LiteralPath $logPath)) { return @() }
    try { return @([System.IO.File]::ReadAllLines($logPath)) }
    catch { return @() }
}

function Assert-GameAlive {
    param([string]$Label)
    $process = Get-Process -Id $script:gamePid -ErrorAction SilentlyContinue
    if (!$process) { throw "DS2 exited while $Label" }
    $process.Refresh()
    if ($process.MainWindowHandle -ne [IntPtr]::Zero -and
        $SI::IsWindowHandle($process.MainWindowHandle)) {
        $script:gameHwnd = $process.MainWindowHandle
    }
    if ($script:gameHwnd -eq [IntPtr]::Zero) {
        throw "DS2 has no valid window while $Label"
    }
}

function Wait-LogLine {
    param([string]$Fragment, [int]$TimeoutMs, [int]$StartLine = 0)
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.ElapsedMilliseconds -lt $TimeoutMs) {
        Assert-GameAlive "waiting for $Fragment"
        $lines = Get-LogLines
        for ($i = [Math]::Max(0, $StartLine); $i -lt $lines.Count; $i++) {
            if ($lines[$i].Contains($Fragment)) { return $true }
        }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

function Send-GameKey {
    param([UInt16]$Scan, [string]$Name, [int]$HoldMs = 60)
    Assert-GameAlive "sending $Name"
    if (!$SI::KeyScan($script:gameHwnd, $Scan, $HoldMs)) {
        throw "could not send $Name"
    }
}

function Wait-GameSeconds {
    param([int]$Seconds, [string]$Label)
    Write-Host "$Label ($($Seconds)s)"
    for ($i = 0; $i -lt $Seconds; $i++) {
        Assert-GameAlive $Label
        Start-Sleep 1
    }
}

Write-Host "=== DS2 fast dismount sequencing test ==="
& (Join-Path $PSScriptRoot "kill_ds2.ps1")
Start-Sleep 2
if (Test-Path -LiteralPath $logPath) {
    Clear-Content -LiteralPath $logPath -ErrorAction SilentlyContinue
}

Start-Process "steam://rungameid/3280350"
$game = $null
for ($i = 0; $i -lt 60; $i++) {
    $candidate = Get-Process -Name "DS2" -ErrorAction SilentlyContinue |
        Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
    if ($candidate) { $game = $candidate; break }
    Start-Sleep 1
}
if (!$game) { throw "DS2 window did not appear" }

if ($SI::GetTitle($game.MainWindowHandle) -eq
    "DEATH STRANDING 2: ON THE BEACH") {
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep 1
        $candidate = Get-Process -Name "DS2" -ErrorAction SilentlyContinue |
            Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
        if ($candidate -and
            $SI::GetTitle($candidate.MainWindowHandle) -match 'v\d') {
            $game = $candidate
            break
        }
    }
}

$script:gamePid = $game.Id
$script:gameHwnd = $game.MainWindowHandle
if ($SI::GetTitle($script:gameHwnd) -notmatch 'v\d') {
    throw "versioned DS2 window not found"
}
Write-Host "DS2 PID=$($game.Id)"
if (!(Wait-LogLine "VehicleBoard] hooks installed" 10000)) {
    throw "hooks were not installed"
}
if (!(Wait-LogLine "mutation=1" 30000)) {
    throw "fast RideOff endpoint was not ready"
}
if (!(Wait-LogLine "dynamic-table clock wrapper installed" 30000)) {
    throw "fast RideOff outer clock was not ready"
}

Wait-GameSeconds 18 "Intro"
Send-GameKey 0x1C "ENTER (skip)"
Wait-GameSeconds 4 "Continue screen"
Send-GameKey 0x1C "ENTER (continue)"
Wait-GameSeconds 1 "Recovery prompt"
Send-GameKey 0x1E "A (yes)"
Wait-GameSeconds 1 "Recovery confirmation"
Send-GameKey 0x1C "ENTER (confirm)"
Wait-GameSeconds 10 "Save loading"

$artifactRoot = Join-Path $PSScriptRoot "artifacts\boarding"
[void](New-Item -ItemType Directory -Force -Path $artifactRoot)
$startLine = (Get-LogLines).Count
Write-Host "Mouse step=$MouseStepX,$MouseStepY interval=${MouseIntervalMs}ms duration=${MouseDurationMs}ms"
$capture = $SI::KeyScanMouseAndCapture(
    $script:gameHwnd, 0x21, 60, $artifactRoot, "fast_dismount",
    $MouseStepX, $MouseStepY, $MouseIntervalMs, $MouseDurationMs)
if (!$capture) { throw "dismount capture failed" }
Write-Host "Capture=$capture"
if (!(Wait-LogLine "RideOff Enter vtable original result=" 3000 $startLine)) {
    throw "F did not enter RideOff; save was not seated or gameplay was not ready"
}
if (!(Wait-LogLine "FastRideOff queue clock advanced" 3000 $startLine)) {
    throw "RideOff queue clock was not synchronized"
}
if (!(Wait-LogLine "FastRideOff terminal pose consumed" 3000 $startLine)) {
    throw "terminal RideOff pose was not grounded and committed"
}
if (!(Wait-LogLine "FastRideOff native fallback clock advanced" 3000 $startLine)) {
    throw "native RideOff completion did not run after pose commit"
}
if (!(Wait-LogLine "callerRva=0xF97B56" 3000 $startLine)) {
    throw "native RideOff OnExit was not observed"
}
$exitLine = Get-LogLines | Select-Object -Skip $startLine |
    Where-Object { $_.Contains("callerRva=0xF97B56") } |
    Select-Object -First 1
if (!$exitLine -or $exitLine -notmatch 'elapsedMs=(\d+)') {
    throw "native RideOff OnExit elapsed time was not recorded"
}
$exitElapsedMs = [int]$Matches[1]
if ($exitElapsedMs -gt 200) {
    throw "RideOff exit was not instant: elapsedMs=$exitElapsedMs"
}
if (!(Wait-LogLine "callerRva=0xFB40B6" 600 $startLine)) {
    throw "Basic action did not enter immediately after RideVehicle exit"
}
if (!(Wait-LogLine "FastRideOff first Basic root rotation leveled" `
        3000 $startLine)) {
    throw "first Basic root rotation was not leveled"
}

$control = $SI::KeyScanAndCapture(
    $script:gameHwnd, 0x1F, 900, $capture, "control_s")
if (!$control) { throw "post-dismount S capture failed" }
Wait-GameSeconds 1 "Control settle"

$rideOffLines = @(Get-LogLines | Select-Object -Skip $startLine)
[System.IO.File]::WriteAllLines(
    (Join-Path $capture "rideoff_log.txt"), [string[]]$rideOffLines)
Copy-Item -LiteralPath $logPath -Destination (
    Join-Path $capture "full_log.txt") -Force
$levelingLines = @($rideOffLines | Where-Object {
    $_.Contains("FastRideOff first Basic root rotation leveled")
})
if ($levelingLines.Count -ne 1) {
    throw "Basic root rotation leveling count was $($levelingLines.Count), expected 1"
}
if ($rideOffLines -match 'callerRva=0x110694D') {
    throw "Fall action entered during the post-dismount control window"
}
$rideOffLines | Where-Object {
    $_ -match 'RideOff|CutIn|graph bool event'
} | ForEach-Object { Write-Host $_ }

Send-GameKey 0x01 "ESC"
Wait-GameSeconds 2 "Quit menu"
Send-GameKey 0x11 "W"
Send-GameKey 0x1C "ENTER"
Wait-GameSeconds 1 "Quit confirmation"
Send-GameKey 0x1E "A"
Send-GameKey 0x1C "ENTER"
for ($i = 0; $i -lt 8; $i++) {
    if (!(Get-Process -Id $script:gamePid -ErrorAction SilentlyContinue)) {
        Write-Host "FAST_DISMOUNT_TRACE_OK=$capture"
        exit 0
    }
    Start-Sleep 1
}
& (Join-Path $PSScriptRoot "kill_ds2.ps1")
Write-Host "FAST_DISMOUNT_TRACE_OK=$capture"
