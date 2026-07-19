param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$OutputDir = "",
    [string]$PackageVersion = "",
    [switch]$SkipBuild,
    [switch]$SkipAsiLoaderDownload
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$repoRoot = $PSScriptRoot
$packageName = "DS2VehicleBoardingFastForward"
$buildRoot = Join-Path $repoRoot "build\package"
$stageRoot = Join-Path $buildRoot "stage"
$gameRoot = Join-Path $stageRoot $packageName
$scriptsDir = Join-Path $gameRoot "scripts"
$cacheDir = Join-Path $buildRoot "cache"
$asiProjectDir = Join-Path $repoRoot "ds2_vehicle_boarding_trace"
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

function Invoke-Build([string]$ProjectDir) {
    $msbuild = Find-MSBuild
    $vcxproj = Join-Path $ProjectDir "ds2_vehicle_boarding_trace.vcxproj"
    $properties = "Configuration=$Configuration;Platform=$Platform"
    & $msbuild $vcxproj `
        "/p:$properties" `
        /t:Rebuild `
        /m /nologo /v:q
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed: $vcxproj"
    }
}

function Copy-RequiredFile([string]$Source, [string]$Destination) {
    if (-not (Test-Path $Source)) {
        throw "Missing required file: $Source"
    }
    New-Item -ItemType Directory -Path (Split-Path $Destination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Get-AsiLoaderZip {
    New-Item -ItemType Directory -Path $cacheDir -Force | Out-Null
    $zip = Join-Path $cacheDir "version-x64.zip"
    Invoke-WebRequest -Uri $asiLoaderUrl -OutFile $zip -Headers @{
        "User-Agent" = "DS2VehicleBoardingFastForward-package-script"
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
New-Item -ItemType Directory -Path $gameRoot -Force | Out-Null
New-Item -ItemType Directory -Path $scriptsDir -Force | Out-Null
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

if (-not $SkipBuild) {
    Invoke-Build -ProjectDir $asiProjectDir
}

# The vcxproj outputs to ds2_vehicle_boarding_trace\build\$(Platform)\$(Configuration)\
$asiName = "ds2_vehicle_boarding_trace.asi"
$builtAsi = Join-Path $asiProjectDir "build\$Platform\$Configuration\$asiName"
if (-not (Test-Path $builtAsi)) {
    throw "Built ASI not found at $builtAsi"
}
Copy-RequiredFile $builtAsi (Join-Path $scriptsDir $asiName)

if (-not $SkipAsiLoaderDownload) {
    Copy-AsiLoader (Get-AsiLoaderZip)
}

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
