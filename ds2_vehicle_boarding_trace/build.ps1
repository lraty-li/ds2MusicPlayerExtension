param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$OutDir = ""
)

$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$vcxproj = Join-Path $projectDir "ds2_vehicle_boarding_trace.vcxproj"
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

if (!(Test-Path $msbuild)) {
    Write-Host "MSBuild not found at $msbuild"
    exit 1
}

$properties = "Configuration=$Configuration;Platform=$Platform"
if ($OutDir) {
    # Ensure trailing backslash
    $OutDir = $OutDir.TrimEnd('\') + '\'
    $properties += ";OutDir=$OutDir"
}

& $msbuild $vcxproj "/p:$properties" /m /nologo /v:q /t:Build
if ($LASTEXITCODE -eq 0) { Write-Host "BUILD_OK" } else { Write-Host "BUILD_FAIL"; exit 1 }
