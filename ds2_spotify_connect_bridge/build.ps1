param(
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $PSScriptRoot "build"
}

Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    $env:CARGO_TARGET_DIR = Join-Path $OutputDir "target"
    $env:CARGO_HOME = Join-Path $OutputDir "cargo-home"
    $env:CARGO_NET_GIT_FETCH_WITH_CLI = "true"
    $env:GIT_CONFIG_GLOBAL = Join-Path $PSScriptRoot "cargo-gitconfig"
    $env:HTTP_PROXY = $null
    $env:HTTPS_PROXY = $null
    $env:ALL_PROXY = $null
    $env:CARGO_HTTP_PROXY = $null

    $cargoArgs = @("build", "--release")
    if (Test-Path (Join-Path $PSScriptRoot "Cargo.lock")) {
        $cargoArgs += "--locked"
    }
    & cargo @cargoArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Cargo build failed with exit code $LASTEXITCODE"
    }

    Copy-Item -LiteralPath (Join-Path $env:CARGO_TARGET_DIR "release\ds2-spotify-connect-bridge.exe") `
      -Destination (Join-Path $OutputDir "DS2SpotifyConnectBridge.exe") -Force
    Write-Host "BUILD_OK"
}
finally {
    Pop-Location
}
