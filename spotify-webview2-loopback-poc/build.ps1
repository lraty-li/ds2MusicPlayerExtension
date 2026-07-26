param(
    [switch]$SkipDependencyDownload
)

$ErrorActionPreference = "Stop"
$packageVersion = "1.0.4078.44"
$projectRoot = $PSScriptRoot
$repoRoot = Split-Path -Parent $projectRoot
$buildRoot = Join-Path $repoRoot "build\spotify-webview2-loopback-poc"
$dependencyRoot = Join-Path $repoRoot "build\deps"
$packageName = "Microsoft.Web.WebView2.$packageVersion"
$packageFile = Join-Path $dependencyRoot "$packageName.nupkg"
$packageZip = Join-Path $dependencyRoot "$packageName.zip"
$packageDir = Join-Path $dependencyRoot $packageName
$packageTargets = Join-Path $packageDir "build\native\Microsoft.Web.WebView2.targets"

function Assert-CodeFileLengths {
    $extensions = @(".cpp", ".h", ".js", ".html", ".css", ".ps1", ".vcxproj")
    $files = Get-ChildItem -LiteralPath $projectRoot -Recurse -File |
        Where-Object { $extensions -contains $_.Extension.ToLowerInvariant() }
    foreach ($file in $files) {
        $count = (Get-Content -Encoding UTF8 -LiteralPath $file.FullName).Count
        if ($count -gt 300) {
            throw "$($file.FullName) has $count lines; limit is 300"
        }
    }
    Write-Host "LINE_LIMIT_OK ($($files.Count) files)"
}

function Restore-WebView2Package {
    if (Test-Path -LiteralPath $packageTargets) {
        return
    }
    if ($SkipDependencyDownload) {
        throw "WebView2 SDK is missing: $packageTargets"
    }
    New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
    if (-not (Test-Path -LiteralPath $packageFile)) {
        $uri = "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$packageVersion"
        Write-Host "Downloading Microsoft.Web.WebView2 $packageVersion"
        Invoke-WebRequest -Uri $uri -OutFile $packageFile
    }
    Copy-Item -LiteralPath $packageFile -Destination $packageZip -Force
    Expand-Archive -LiteralPath $packageZip -DestinationPath $packageDir
    if (-not (Test-Path -LiteralPath $packageTargets)) {
        throw "WebView2 SDK extraction failed"
    }
}

function Find-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        return $null
    }
    $installPath = & $vswhere -latest -products * `
        -requires Microsoft.Component.MSBuild -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installPath) {
        return $null
    }
    $candidate = Join-Path $installPath.Trim() "MSBuild\Current\Bin\amd64\MSBuild.exe"
    if (Test-Path -LiteralPath $candidate) {
        return $candidate
    }
    return $null
}

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

Assert-CodeFileLengths
Restore-WebView2Package
Repair-ProcessPath

$msbuild = Find-MSBuild
if (-not $msbuild) {
    throw "MSBuild was not found"
}

$outputDir = Join-Path $buildRoot "Release"
$intermediateDir = Join-Path $buildRoot "obj"
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
New-Item -ItemType Directory -Path $intermediateDir -Force | Out-Null
$project = Join-Path $projectRoot "spotify_webview2_loopback_poc.vcxproj"
$properties =
    "Configuration=Release;Platform=x64;OutDir=$outputDir\;" +
    "IntDir=$intermediateDir\;WebView2PackageDir=$packageDir"

& $msbuild $project "/p:$properties" /m /nologo /v:q
if ($LASTEXITCODE -ne 0) {
    Write-Host "BUILD_FAIL"
    exit $LASTEXITCODE
}

$probeProject = Join-Path $projectRoot "spotify_game_stream_probe.vcxproj"
$probeIntermediateDir = Join-Path $buildRoot "probe-obj"
New-Item -ItemType Directory -Path $probeIntermediateDir -Force | Out-Null
$probeProperties =
    "Configuration=Release;Platform=x64;OutDir=$outputDir\;" +
    "IntDir=$probeIntermediateDir\"
& $msbuild $probeProject "/p:$probeProperties" /m /nologo /v:q
if ($LASTEXITCODE -ne 0) {
    Write-Host "PROBE_BUILD_FAIL"
    exit $LASTEXITCODE
}

$webOutput = Join-Path $outputDir "web"
New-Item -ItemType Directory -Path $webOutput -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $projectRoot "web") -File |
    Copy-Item -Destination $webOutput -Force
Copy-Item -LiteralPath (Join-Path $projectRoot "config.json") `
    -Destination (Join-Path $outputDir "config.json") -Force
Write-Host "BUILD_OK"
Write-Host (Join-Path $outputDir "spotify_webview2_loopback_poc.exe")
Write-Host (Join-Path $outputDir "spotify_game_stream_probe.exe")
