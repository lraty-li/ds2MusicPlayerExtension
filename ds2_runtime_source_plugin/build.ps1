param(
    [string]$OutputDir = "",
    [string]$IntermediateDir = ""
)

$ErrorActionPreference = "Stop"

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
$solution = Join-Path $PSScriptRoot "ds2_dll_music_resource.sln"

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

if (-not (Test-Path $msbuild)) {
    Write-Host "MSBUILD_NOT_FOUND"
    exit 2
}

if (-not (Test-Path $solution)) {
    Write-Host "SOLUTION_NOT_FOUND"
    exit 3
}

$properties = "Configuration=Release;Platform=x64"
if (-not [string]::IsNullOrWhiteSpace($OutputDir)) {
    $resolvedOutputDir =
        $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
    New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null
    if (-not $resolvedOutputDir.EndsWith("\")) {
        $resolvedOutputDir += "\"
    }
    $properties = "$properties;OutDir=$resolvedOutputDir"
}
if (-not [string]::IsNullOrWhiteSpace($IntermediateDir)) {
    $resolvedIntermediateDir =
        $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($IntermediateDir)
    New-Item -ItemType Directory -Path $resolvedIntermediateDir -Force | Out-Null
    if (-not $resolvedIntermediateDir.EndsWith("\")) {
        $resolvedIntermediateDir += "\"
    }
    $properties = "$properties;IntDir=$resolvedIntermediateDir"
}

& $msbuild $solution "/p:$properties" /m /nologo /v:q
if ($LASTEXITCODE -eq 0) {
    Write-Host "BUILD_OK"
    exit 0
}

Write-Host "BUILD_FAIL"
exit $LASTEXITCODE
