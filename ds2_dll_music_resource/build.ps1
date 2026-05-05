$ErrorActionPreference = "Stop"

$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\Msbuild\Current\Bin\amd64\MSBuild.exe"
$solution = Join-Path $PSScriptRoot "ds2_dll_music_resource.sln"

if (-not (Test-Path $msbuild)) {
    Write-Host "MSBUILD_NOT_FOUND"
    exit 2
}

if (-not (Test-Path $solution)) {
    Write-Host "SOLUTION_NOT_FOUND"
    exit 3
}

& $msbuild $solution "/p:Configuration=Release;Platform=x64" /m /nologo /v:q
if ($LASTEXITCODE -eq 0) {
    Write-Host "BUILD_OK"
    exit 0
}

Write-Host "BUILD_FAIL"
exit $LASTEXITCODE
