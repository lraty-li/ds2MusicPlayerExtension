$ErrorActionPreference = "Stop"

$wwiseSdkRoot = "E:\dev\sdk\wwise\Wwise2023.1.8.8601"
$wwiseAuthoringRoot = "E:\dev\sdk\wwise\Wwise2023.1.8.8601"
$project = Join-Path $PSScriptRoot "ds2_dll_music_resource_authoring.vcxproj"
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

$pathValue = [Environment]::GetEnvironmentVariable("Path", "Process")
if (-not $pathValue) {
    $pathValue = [Environment]::GetEnvironmentVariable("PATH", "Process")
}
Remove-Item Env:PATH -ErrorAction SilentlyContinue
Remove-Item Env:Path -ErrorAction SilentlyContinue
$env:Path = $pathValue

if (-not $msbuild) {
    Write-Host "MSBUILD_NOT_FOUND"
    exit 2
}

& $msbuild $project `
    /p:Configuration=Release `
    /p:Platform=x64 `
    "/p:WwiseRoot=$wwiseSdkRoot" `
    "/p:WwiseAuthoringRoot=$wwiseAuthoringRoot" `
    /m `
    /nologo

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
