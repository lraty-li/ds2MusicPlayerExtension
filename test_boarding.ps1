$ErrorActionPreference = "Stop"

$gameDir = "F:\SteamLibrary\steamapps\common\DEATH STRANDING 2 - ON THE BEACH"
$logPath = Join-Path $gameDir "log.txt"
$inputSource = Join-Path $PSScriptRoot "tools\BoardingTestInput.cs"
Add-Type -Path $inputSource
try {
    Add-Type -AssemblyName System.Drawing.Common -ErrorAction Stop
}
catch {
    Add-Type -AssemblyName System.Drawing
}
$SI = [BoardingTestInput]

Write-Host "=== DS2 Fast Boarding Mod Test ==="
Write-Host "Cleaning stale game processes..."
& (Join-Path $PSScriptRoot "kill_ds2.ps1")
Start-Sleep 2
if (Test-Path $logPath) { Clear-Content $logPath -ErrorAction SilentlyContinue }

$script:gamePid = 0
$script:gameHwnd = [IntPtr]::Zero

function Get-CrashReportProcesses {
    @(Get-Process -Name "crs-handler" -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowTitle -eq "Report Problem" })
}

function Close-CrashReportWindow {
    foreach ($report in (Get-CrashReportProcesses)) {
        Write-Host "Closing crash report window (pid $($report.Id))"
        [void]$report.CloseMainWindow()
        Start-Sleep -Milliseconds 300
        if (Get-Process -Id $report.Id -ErrorAction SilentlyContinue) {
            Stop-Process -Id $report.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

function Stop-FailedTest {
    param([string]$Reason)
    Close-CrashReportWindow
    Write-Host "FAIL: $Reason"
    & (Join-Path $PSScriptRoot "kill_ds2.ps1")
    exit 1
}

function Assert-GameAlive {
    param([string]$Before)
    if ((Get-CrashReportProcesses).Count -gt 0) {
        Stop-FailedTest "DS2 crashed before $Before (Report Problem opened)"
    }
    $process = Get-Process -Id $script:gamePid -ErrorAction SilentlyContinue
    if (!$process) { Stop-FailedTest "DS2 exited before $Before" }
    if ($script:gameHwnd -eq [IntPtr]::Zero -or
        -not $SI::IsWindowHandle($script:gameHwnd)) {
        for ($attempt = 1; $attempt -le 15; $attempt++) {
            $process = Get-Process -Id $script:gamePid -ErrorAction SilentlyContinue
            if (!$process) { Stop-FailedTest "DS2 exited before $Before" }
            $process.Refresh()
            $candidateHwnd = $process.MainWindowHandle
            if ($candidateHwnd -ne [IntPtr]::Zero -and
                $SI::IsWindowHandle($candidateHwnd)) {
                $script:gameHwnd = $candidateHwnd
                Write-Host "  Rebound DS2 hwnd=0x$($candidateHwnd.ToString('X'))"
                return
            }
            Start-Sleep -Milliseconds 200
        }
        Stop-FailedTest "DS2 had no valid window for 3s before $Before"
    }
}

function Wait-GameSeconds {
    param([string]$Label, [int]$Seconds)
    Write-Host "$Label ($($Seconds)s)"
    for ($i = 0; $i -lt $Seconds; $i++) {
        Assert-GameAlive "waiting for $Label"
        Start-Sleep 1
    }
}

function Get-LogLines {
    if (!(Test-Path $logPath)) { return @() }
    try { return @([System.IO.File]::ReadAllLines($logPath)) }
    catch { return @() }
}

function Wait-LogLine {
    param(
        [string]$Fragment,
        [int]$TimeoutMs,
        [string]$Label,
        [int]$StartLine = 0
    )
    $deadline = [Environment]::TickCount64 + $TimeoutMs
    while ([Environment]::TickCount64 -lt $deadline) {
        Assert-GameAlive $Label
        $lines = Get-LogLines
        for ($i = [Math]::Max(0, $StartLine); $i -lt $lines.Count; $i++) {
            if ($lines[$i].Contains($Fragment)) {
                Write-Host "  Confirmed $Label"
                return $true
            }
        }
        Start-Sleep -Milliseconds 200
    }
    return $false
}
function Send-GameKey {
    param([UInt16]$Scan, [string]$Name, [int]$HoldMs = 60)
    Assert-GameAlive "sending $Name"
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        Write-Host "  Focus + $Name attempt $attempt (scan 0x$($Scan.ToString('X')))"
        if ($SI::KeyScan($script:gameHwnd, $Scan, $HoldMs)) { return }
        Start-Sleep -Milliseconds 300
    }
    Stop-FailedTest "could not focus DS2 for $Name"
}

function Capture-GameWindow {
    param([string]$Path)
    $bounds = $SI::GetWindowBounds($script:gameHwnd)
    if (!$bounds) { return $false }
    $width = $bounds[2] - $bounds[0]
    $height = $bounds[3] - $bounds[1]
    if ($width -le 0 -or $height -le 0) { return $false }
    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen(
            $bounds[0], $bounds[1], 0, 0,
            [System.Drawing.Size]::new($width, $height),
            [System.Drawing.CopyPixelOperation]::SourceCopy)
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
    return $true
}

Write-Host "Launching via Steam..."
Start-Process "steam://rungameid/3280350"
$game = $null
for ($i = 0; $i -lt 60; $i++) {
    $candidate = Get-Process -Name "DS2" -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($candidate) { $game = $candidate; break }
    Start-Sleep 1
}
if (!$game) { Stop-FailedTest "DS2 window did not appear" }

$title = $SI::GetTitle($game.MainWindowHandle)
if ($title -eq "DEATH STRANDING 2: ON THE BEACH") {
    Write-Host "Launcher detected; waiting for the game window"
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep 1
        $candidate = Get-Process -Name "DS2" -ErrorAction SilentlyContinue |
            Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
        if ($candidate -and $SI::GetTitle($candidate.MainWindowHandle) -match 'v\d') {
            $game = $candidate
            break
        }
    }
}

$script:gamePid = $game.Id
$script:gameHwnd = $game.MainWindowHandle
$title = $SI::GetTitle($script:gameHwnd)
if ($title -notmatch 'v\d') { Stop-FailedTest "versioned game window not found" }
Write-Host "DS2 PID: $($game.Id) hwnd=0x$($script:gameHwnd.ToString('X')) title=`"$title`""

if (!(Wait-LogLine "VehicleBoard] hooks installed" 10000 "boarding hooks")) {
    Stop-FailedTest "boarding hooks were not fully installed"
}

Wait-GameSeconds "Intro" 18
Send-GameKey 0x1C "ENTER (SKIP)"
Wait-GameSeconds "Continue screen" 4
Send-GameKey 0x1C "ENTER (CONTINUE)"
Wait-GameSeconds "Recover prompt" 1
Send-GameKey 0x1E "A (RECOVER YES)"
Wait-GameSeconds "Recover confirm" 1
Send-GameKey 0x1C "ENTER (CONFIRM RECOVER)"
Wait-GameSeconds "Initial load" 4

if (!(Wait-LogLine "FastBoarding MOD READY" 30000 `
        "all fast-boarding components")) {
    Stop-FailedTest "fast-boarding components were not ready before boarding"
}
if (!(Wait-LogLine "RideOn ProcessAttach vtable observer installed" 30000 `
        "RideOn vtable observer")) {
    Stop-FailedTest "RideOn vtable observer was not installed before boarding"
}
if (!(Wait-LogLine "RideOn Enter vtable observer installed" 30000 `
        "RideOn Enter vtable observer")) {
    Stop-FailedTest "RideOn Enter vtable observer was not installed before boarding"
}
if (!(Wait-LogLine "Drive Enter vtable observer installed" 30000 `
        "Drive vtable observer")) {
    Stop-FailedTest "Drive vtable observer was not installed before boarding"
}
if (!(Wait-LogLine "RideOn Update vtable observer installed" 30000 `
        "RideOn Update vtable observer")) {
    Stop-FailedTest "RideOn Update vtable observer was not installed before boarding"
}

$boarded = $false
for ($attempt = 1; $attempt -le 3 -and !$boarded; $attempt++) {
    $startLine = (Get-LogLines).Count
    Send-GameKey 0x21 "F (BOARD $attempt/3)"
    $boarded = Wait-LogLine "FastBoarding descriptor evaluated" 3000 `
        "left-front fast descriptor" $startLine
    if (!$boarded) {
        Write-Host "  No RideOn event; waiting before retry"
        Wait-GameSeconds "Board retry delay" 1
    }
}
if (!$boarded) { Stop-FailedTest "three BOARD inputs produced no RideOn event" }

if (!(Wait-LogLine "complete=1" 2000 `
        "fast character descriptor completion" $startLine)) {
    Stop-FailedTest "character descriptor did not reach a valid fast result"
}
if (!(Wait-LogLine "finished=1" 2000 `
        "CutIn playback finished" $startLine)) {
    Stop-FailedTest "CutIn playback did not finish through its native update"
}
if (!(Wait-LogLine "FastBoarding CutIn Deactivate clean=1" 2000 `
        "CutIn normal Deactivate" $startLine)) {
    Stop-FailedTest "CutIn did not exit through normal Deactivate"
}
if (!(Wait-LogLine "FastBoarding emitted native RideOn completion event" 2000 `
        "RideOn completion event" $startLine)) {
    Stop-FailedTest "RideOn completion was not released after CutIn cleanup"
}
if (!(Wait-LogLine "DriveVtable original result=" 2000 `
        "Drive Enter" $startLine)) {
    Stop-FailedTest "RideOn did not transition to Drive"
}

$captureDir = Join-Path $PSScriptRoot "artifacts\boarding"
[void](New-Item -ItemType Directory -Force -Path $captureDir)
foreach ($capture in @(
    @{ Name = "drive_0200ms.png"; Delay = 0 },
    @{ Name = "drive_0700ms.png"; Delay = 500 },
    @{ Name = "drive_1700ms.png"; Delay = 1000 }
)) {
    if ($capture.Delay -gt 0) { Start-Sleep -Milliseconds $capture.Delay }
    $capturePath = Join-Path $captureDir $capture.Name
    if (Capture-GameWindow $capturePath) {
        Write-Host "  Captured $capturePath"
    }
}

Wait-GameSeconds "Fast Drive settle" 1
[void](Capture-GameWindow (Join-Path $captureDir "drive_settled.png"))

$dismountStartLine = (Get-LogLines).Count
Send-GameKey 0x21 "F (DISMOUNT)"
if (!(Wait-LogLine "RideOff animation state requested=1" 750 `
        "accelerated RideOff completion" $dismountStartLine)) {
    Stop-FailedTest "RideOff animation did not complete within 750ms"
}
$rideOffLine = Get-LogLines | Where-Object { $_.Contains("RideOff animation state requested=1") } | Select-Object -Last 1
if ($rideOffLine -notmatch 'elapsedMs=(\d+)' -or [int]$Matches[1] -gt 750) { Stop-FailedTest "RideOff completion timing was invalid" }
foreach ($capture in @(
    @{ Name = "dismount_0200ms.png"; Delay = 200 },
    @{ Name = "dismount_0700ms.png"; Delay = 500 },
    @{ Name = "dismount_1700ms.png"; Delay = 1000 }
)) {
    Start-Sleep -Milliseconds $capture.Delay
    [void](Capture-GameWindow (Join-Path $captureDir $capture.Name))
}
Wait-GameSeconds "Native dismount settle" 5
[void](Capture-GameWindow (Join-Path $captureDir "dismount1_settled.png"))

for ($quitAttempt = 1; $quitAttempt -le 3; $quitAttempt++) {
    Write-Host "Quit sequence attempt $quitAttempt/3"
    Send-GameKey 0x01 "ESC"
    Wait-GameSeconds "Quit menu" 2
    Send-GameKey 0x11 "W"
    Start-Sleep -Milliseconds 200
    Send-GameKey 0x1C "ENTER"
    Wait-GameSeconds "Quit confirm" 1
    Send-GameKey 0x1E "A"
    Start-Sleep -Milliseconds 200
    Send-GameKey 0x1C "ENTER"

    for ($i = 0; $i -lt 7; $i++) {
        if (!(Get-Process -Id $script:gamePid -ErrorAction SilentlyContinue)) {
            Write-Host "=== PASS: fast boarding, Drive, dismount, and quit confirmed ==="
            exit 0
        }
        Start-Sleep 1
    }
}
Stop-FailedTest "game did not exit after quit confirmation"
