param(
    [ValidateRange(1024, 65535)]
    [int]$Port = 8765
)

$ErrorActionPreference = "Stop"
$siteRoot = $PSScriptRoot
$siteUrl = "http://127.0.0.1:$Port/"

Write-Host "DS2 Spotify Web Playback SDK PoC"
Write-Host "URL:          $siteUrl"
Write-Host "Redirect URI: $siteUrl"
Write-Host "Press Ctrl+C to stop."

Push-Location $siteRoot
try {
    python -m http.server $Port --bind 127.0.0.1
}
finally {
    Pop-Location
}
