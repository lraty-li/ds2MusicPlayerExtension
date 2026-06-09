param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$OutputDir = "",
    [string]$PackageVersion = "",
    [switch]$SkipBuild,
    [switch]$SkipAsiLoaderDownload,
    [switch]$AllowMissingBc7e
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$repoRoot = $PSScriptRoot
$packageName = "DS2MusicPlayer"
$buildRoot = Join-Path $repoRoot "build\package"
$stageRoot = Join-Path $buildRoot "stage"
$gameRoot = Join-Path $stageRoot $packageName
$scriptsDir = Join-Path $gameRoot "scripts"
$extensionDir = Join-Path $stageRoot "browser-extension"
$cacheDir = Join-Path $buildRoot "cache"
$binDir = Join-Path $buildRoot "bin"
$bc7eDll = Join-Path $repoRoot "third_party\bc7e\bin\win64\ds2_jacket_bc7e.dll"
$asiLoaderUrl =
    "https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/x64-latest/version-x64.zip"

function Repair-ProcessPath {
    $pathValue = [Environment]::GetEnvironmentVariable("Path", "Process")
    if ([string]::IsNullOrWhiteSpace($pathValue)) {
        $machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
        $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
        $pathValue = "$machinePath;$userPath"
    }
    [Environment]::SetEnvironmentVariable("PATH", $null, "Process")
    [Environment]::SetEnvironmentVariable("Path", $pathValue, "Process")
}

Repair-ProcessPath

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "dist"
}

function New-CleanDirectory([string]$Path) {
    if (Test-Path $Path) {
        $resolved = (Resolve-Path $Path).Path
        $root = (Resolve-Path $repoRoot).Path
        if (-not $resolved.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean outside repository: $resolved"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

function Find-MSBuild {
    $paths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Msbuild\Current\Bin\amd64\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\Msbuild\Current\Bin\amd64\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Msbuild\Current\Bin\amd64\MSBuild.exe"
    )
    foreach ($path in $paths) {
        if (Test-Path $path) { return $path }
    }
    throw "MSBuild.exe not found"
}

function Invoke-Build([string]$Solution, [string]$ProjectOutDir) {
    $msbuild = Find-MSBuild
    New-Item -ItemType Directory -Path $ProjectOutDir -Force | Out-Null
    $properties = "Configuration=$Configuration;Platform=$Platform;OutDir=$ProjectOutDir\"
    & $msbuild $Solution `
        "/p:$properties" `
        /m /nologo /v:q
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed: $Solution"
    }
}

function Copy-RequiredFile([string]$Source, [string]$Destination) {
    if (-not (Test-Path $Source)) {
        throw "Missing required file: $Source"
    }
    New-Item -ItemType Directory -Path (Split-Path $Destination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Assert-Bc7eDll([string]$Path) {
    if (-not (Test-Path $Path)) {
        if ($AllowMissingBc7e) {
            Write-Host "BC7E_DLL_NOT_FOUND fallback encoder will be used"
            return $false
        }
        throw "Missing BC7E DLL. Run tools\build-bc7e.ps1 first."
    }
    & (Join-Path $repoRoot "tools\verify-bc7e.ps1") -DllPath $Path
    if ($LASTEXITCODE -ne 0) {
        throw "BC7E DLL verification failed: $Path"
    }
    return $true
}

function Copy-BrowserExtension {
    $source = Join-Path $repoRoot "tab-audio-recorder-mvp"
    $files = @(
        "manifest.json",
        "service_worker.js",
        "media_control.js",
        "page_control.js",
        "adapters\youtube.js",
        "adapters\netease.js",
        "adapters\media_session_hook.js",
        "offscreen.html",
        "offscreen.js",
        "pcm-worklet.js",
        "README.md"
    )
    New-Item -ItemType Directory -Path $extensionDir -Force | Out-Null
    foreach ($file in $files) {
        Copy-RequiredFile (Join-Path $source $file) (Join-Path $extensionDir $file)
    }
}

function Get-AsiLoaderZip {
    New-Item -ItemType Directory -Path $cacheDir -Force | Out-Null
    $zip = Join-Path $cacheDir "version-x64.zip"
    Invoke-WebRequest -Uri $asiLoaderUrl -OutFile $zip -Headers @{
        "User-Agent" = "DS2MusicPlayer-package-script"
    }
    return $zip
}

function Copy-AsiLoader([string]$ZipPath) {
    $extract = Join-Path $cacheDir "Ultimate-ASI-Loader"
    New-CleanDirectory $extract
    Expand-Archive -Path $ZipPath -DestinationPath $extract -Force
    $candidates = Get-ChildItem -Path $extract -Recurse -Filter "version.dll" -File
    $selected = $candidates |
        Where-Object { $_.FullName -match "(?i)(x64|win64)" } |
        Select-Object -First 1
    if (-not $selected) {
        $selected = $candidates | Select-Object -First 1
    }
    if (-not $selected) {
        throw "version.dll not found in Ultimate-ASI-Loader archive"
    }
    Copy-RequiredFile $selected.FullName (Join-Path $gameRoot "version.dll")
}

function Write-InstallReadme {
    Copy-RequiredFile (Join-Path $repoRoot "packaging\README.txt") `
        (Join-Path $stageRoot "README.txt")
}

New-CleanDirectory $stageRoot
New-Item -ItemType Directory -Path $scriptsDir -Force | Out-Null
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

if (-not $SkipBuild) {
    New-CleanDirectory $binDir
    Invoke-Build (Join-Path $repoRoot "ds2_music_player_asi\Ds2MusicPlayerExtend.sln") `
        (Join-Path $binDir "asi")
    Invoke-Build (Join-Path $repoRoot "ds2_runtime_source_plugin\ds2_dll_music_resource.sln") `
        (Join-Path $binDir "audio")
}

Copy-RequiredFile (Join-Path $binDir "asi\Ds2MusicPlayerExtend.asi") `
    (Join-Path $scriptsDir "Ds2MusicPlayerExtend.asi")
Copy-RequiredFile (Join-Path $binDir "audio\ds2_dll_music_resource.dll") `
    (Join-Path $scriptsDir "ds2_dll_music_resource.dll")
if (Assert-Bc7eDll $bc7eDll) {
    Copy-RequiredFile $bc7eDll (Join-Path $scriptsDir "ds2_jacket_bc7e.dll")
}

if (-not $SkipAsiLoaderDownload) {
    Copy-AsiLoader (Get-AsiLoaderZip)
}

Copy-BrowserExtension
Write-InstallReadme

$stamp = if ([string]::IsNullOrWhiteSpace($PackageVersion)) {
    Get-Date -Format "yyyyMMdd-HHmmss"
} else {
    $PackageVersion
}
$zipPath = Join-Path $OutputDir "$packageName-$stamp.zip"
if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $stageRoot "*") -DestinationPath $zipPath -Force
if ($env:GITHUB_OUTPUT) {
    "zip_path=$zipPath" | Add-Content -Path $env:GITHUB_OUTPUT -Encoding UTF8
}
Write-Host "PACKAGE_OK $zipPath"
