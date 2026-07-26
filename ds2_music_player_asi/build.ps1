param(
    [ValidateSet("Base", "Spotify")]
    [string]$Edition = "Spotify"
)

$ErrorActionPreference = "Stop"

$pathValue = $env:Path
if (-not $pathValue) {
    $pathValue = $env:PATH
}
[Environment]::SetEnvironmentVariable("PATH", $null, "Process")
if ($pathValue) {
    [Environment]::SetEnvironmentVariable("Path", $pathValue, "Process")
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = $null
if (Test-Path -LiteralPath $vswhere) {
    $vsInstallPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if ($LASTEXITCODE -eq 0 -and $vsInstallPath) {
        $candidate = Join-Path $vsInstallPath.Trim() "MSBuild\Current\Bin\amd64\MSBuild.exe"
        if (Test-Path -LiteralPath $candidate) {
            $msbuild = $candidate
        }
    }
}
$solution = Join-Path $PSScriptRoot "Ds2MusicPlayerExtend.sln"

if (-not (Test-Path $msbuild)) {
    Write-Host "MSBUILD_NOT_FOUND"
    exit 2
}

if (-not (Test-Path $solution)) {
    Write-Host "SOLUTION_NOT_FOUND"
    exit 3
}

$spotifyConnect = ($Edition -eq "Spotify").ToString().ToLowerInvariant()
$properties = "Configuration=Release;Platform=x64;Ds2SpotifyConnect=$spotifyConnect"
& $msbuild $solution "/p:$properties" /m /nologo /v:q
if ($LASTEXITCODE -eq 0) {
    Write-Host "BUILD_OK"
    exit 0
}

Write-Host "BUILD_FAIL"
exit $LASTEXITCODE
