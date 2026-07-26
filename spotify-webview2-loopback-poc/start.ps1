$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $repoRoot `
    "build\spotify-webview2-loopback-poc\Release\spotify_webview2_loopback_poc.exe"

if (-not (Test-Path -LiteralPath $executable)) {
    & (Join-Path $PSScriptRoot "build.ps1")
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$sourceConfig = Join-Path $PSScriptRoot "config.json"
$outputConfig = Join-Path (Split-Path -Parent $executable) "config.json"
Copy-Item -LiteralPath $sourceConfig -Destination $outputConfig -Force

& $executable
