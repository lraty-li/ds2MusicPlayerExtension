$project = "ds2_vehicle_boarding_trace"
$sourceDir = "E:\dev\code\game\DS2MusicPlayer\$project"
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

if (!(Test-Path $msbuild)) {
    Write-Host "MSBuild not found at $msbuild"
    exit 1
}

Set-Location $sourceDir
& $msbuild "ds2_vehicle_boarding_trace.vcxproj" /p:Configuration=Release /p:Platform=x64 /t:Build
if ($LASTEXITCODE -eq 0) { Write-Host "BUILD_OK" } else { Write-Host "BUILD_FAIL"; exit 1 }
