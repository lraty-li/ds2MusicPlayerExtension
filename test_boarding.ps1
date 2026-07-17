$ErrorActionPreference = "Stop"

$gameDir = "F:\SteamLibrary\steamapps\common\DEATH STRANDING 2 - ON THE BEACH"
$logPath = Join-Path $gameDir "log.txt"
$inputSource = Join-Path $PSScriptRoot "tools\BoardingTestInput.cs"
Add-Type -Path $inputSource
Add-Type -AssemblyName System.Drawing.Common
$SI = [BoardingTestInput]

Write-Host "=== DS2 Boarding Test (event-gated SendInput) ==="
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

if (!(Wait-LogLine "FullGame animation read-only hooks installed" 30000 `
        "fullgame animation trace")) {
    Stop-FailedTest "fullgame animation trace was not installed before boarding"
}
if (!(Wait-LogLine "FullGame result-channel read-only hook installed" 15000 `
        "fullgame result-channel trace")) {
    Stop-FailedTest "fullgame result-channel trace was not installed before boarding"
}

$boarded = $false
for ($attempt = 1; $attempt -le 3 -and !$boarded; $attempt++) {
    $startLine = (Get-LogLines).Count
    Send-GameKey 0x21 "F (BOARD $attempt/3)"
    $boarded = Wait-LogLine "ProcessAttach original stage 0->1" 3000 `
        "RideOn stage 0->1" $startLine
    if (!$boarded) {
        Write-Host "  No RideOn event; waiting before retry"
        Wait-GameSeconds "Board retry delay" 1
    }
}
if (!$boarded) { Stop-FailedTest "three BOARD inputs produced no RideOn event" }

if (!(Wait-LogLine "DriveEnter exit" 8000 "DriveEnter")) {
    Stop-FailedTest "RideOn started but never reached DriveEnter"
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

# Keep boarding and dismount visually separate. DriveEnter can occur before the
# presentation graph has converged, especially in fast-boarding experiments.
Wait-GameSeconds "Boarded dwell" 4

$dismountStart = (Get-LogLines).Count
Send-GameKey 0x21 "F (DISMOUNT)"
if (!(Wait-LogLine " start=0 finishFlag=0" 8000 "seat transition finish" $dismountStart)) {
    Stop-FailedTest "DISMOUNT produced no seat transition finish"
}

Wait-GameSeconds "Post-dismount settle" 1
[void](Capture-GameWindow (Join-Path $captureDir "dismount1_settled.png"))

Send-GameKey 0x20 "D (RETURN TO VEHICLE)" 900
Start-Sleep -Milliseconds 300
$secondBoardStart = (Get-LogLines).Count
Send-GameKey 0x21 "F (BOARD SECOND CYCLE)"
if (!(Wait-LogLine "ProcessAttach original stage 0->1" 5000 `
        "second RideOn stage 0->1" $secondBoardStart)) {
    Stop-FailedTest "second BOARD produced no RideOn event"
}
if (!(Wait-LogLine "DriveEnter exit" 8000 "second DriveEnter" $secondBoardStart)) {
    Stop-FailedTest "second RideOn never reached DriveEnter"
}
[void](Capture-GameWindow (Join-Path $captureDir "drive2_0200ms.png"))
Start-Sleep -Milliseconds 500
[void](Capture-GameWindow (Join-Path $captureDir "drive2_0700ms.png"))
Start-Sleep -Milliseconds 1000
[void](Capture-GameWindow (Join-Path $captureDir "drive2_1700ms.png"))
Wait-GameSeconds "Second boarded dwell" 2

$secondDismountStart = (Get-LogLines).Count
Send-GameKey 0x21 "F (DISMOUNT SECOND CYCLE)"
if (!(Wait-LogLine " start=0 finishFlag=0" 8000 `
        "second seat transition finish" $secondDismountStart)) {
    Stop-FailedTest "second DISMOUNT produced no seat transition finish"
}
Wait-GameSeconds "Second post-dismount settle" 1
[void](Capture-GameWindow (Join-Path $captureDir "dismount2_settled.png"))

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
            Write-Host "=== PASS: two board/drive/dismount cycles and quit confirmed ==="
            exit 0
        }
        Start-Sleep 1
    }
}
Stop-FailedTest "game did not exit after quit confirmation"
