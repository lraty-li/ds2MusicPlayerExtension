$ErrorActionPreference = "Stop"

$wwiseSdkRoot = "E:\dev\sdk\wwise\Wwise2023.1.8.8601"
$wwiseAuthoringRoot = "E:\dev\sdk\wwise\Wwise2023.1.8.8601"
$project = Join-Path $PSScriptRoot "ds2_dll_music_resource_authoring.vcxproj"
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\Msbuild\Current\Bin\amd64\MSBuild.exe"

$pathValue = [Environment]::GetEnvironmentVariable("Path", "Process")
if (-not $pathValue) {
    $pathValue = [Environment]::GetEnvironmentVariable("PATH", "Process")
}
Remove-Item Env:PATH -ErrorAction SilentlyContinue
Remove-Item Env:Path -ErrorAction SilentlyContinue
$env:Path = $pathValue

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
