param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$OutDir = ""
)

$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$vcxproj = Join-Path $projectDir "ds2_vehicle_boarding_trace.vcxproj"
$vswhere = Join-Path ${env:ProgramFiles(x86)} `
    "Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = $null

if (Test-Path $vswhere) {
    $installRoot = (& $vswhere -latest -products * `
        -property installationPath | Select-Object -First 1).Trim()
    if ($installRoot) {
        $candidate = Join-Path $installRoot "MSBuild\Current\Bin\MSBuild.exe"
        if (Test-Path $candidate) {
            $msbuild = $candidate
        }
    }
}

if (!$msbuild) {
    Write-Host "MSBuild not found through vswhere at $vswhere"
    exit 1
}

$vcvars = Join-Path $installRoot "VC\Auxiliary\Build\vcvars64.bat"
if ($Platform -ne "x64" -or !(Test-Path $vcvars)) {
    Write-Host "x64 vcvars environment not found at $vcvars"
    exit 1
}

$properties = "Configuration=$Configuration;Platform=$Platform"
if ($OutDir) {
    # Ensure trailing backslash
    $OutDir = $OutDir.TrimEnd('\') + '\'
    $properties += ";OutDir=$OutDir"
}

$buildCommand = "set `"Path=`" && set `"PATH=%SystemRoot%\System32;%SystemRoot%`" " +
    "&& call `"$vcvars`" && `"$msbuild`" `"$vcxproj`" " +
    "`"/p:$properties`" /m /nologo /v:q /t:Build"
& $env:ComSpec /d /c $buildCommand
if ($LASTEXITCODE -eq 0) { Write-Host "BUILD_OK" } else { Write-Host "BUILD_FAIL"; exit 1 }
