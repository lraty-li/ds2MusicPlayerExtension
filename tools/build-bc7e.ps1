param(
    [string]$SourceDir = "",
    [string]$InstallDir = "",
    [string]$GameScriptsDir = "",
    [string]$Commit = "dbe416d28a5530b4e8cc45b14bf034dc6b96bbde",
    [switch]$SkipFetch
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent
if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Join-Path $repoRoot "build\bc7e\bc7enc_rdo"
}
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $repoRoot "third_party\bc7e\bin\win64"
}

function Require-Command([string]$Name) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $cmd) { throw "$Name not found in PATH" }
    return $cmd.Source
}

function Resolve-MsBuild {
    $path = "C:\Program Files\Microsoft Visual Studio\2022\Community\Msbuild\Current\Bin\amd64\MSBuild.exe"
    if (Test-Path $path) { return $path }
    throw "MSBuild not found"
}

function Resolve-Ispc([string]$Dir) {
    $local = Join-Path $Dir "ispc.exe"
    if (Test-Path $local) { return (Resolve-Path $local).Path }
    return Require-Command "ispc"
}

$git = Require-Command "git"
$msbuild = Resolve-MsBuild

New-Item -ItemType Directory -Path (Split-Path $SourceDir -Parent) -Force | Out-Null
if (-not (Test-Path $SourceDir)) {
    & $git clone "https://github.com/richgel999/bc7enc_rdo.git" $SourceDir
    if ($LASTEXITCODE -ne 0) { throw "git clone failed" }
}

if (-not $SkipFetch) {
    & $git -C $SourceDir fetch --all --tags
    if ($LASTEXITCODE -ne 0) { throw "git fetch failed" }
}
& $git -C $SourceDir checkout $Commit
if ($LASTEXITCODE -ne 0) { throw "git checkout failed" }
$actualCommit = (& $git -C $SourceDir rev-parse HEAD).Trim()

$ispc = Resolve-Ispc $SourceDir
$generatedDir = Join-Path $repoRoot "build\bc7e\generated"
New-Item -ItemType Directory -Path $generatedDir -Force | Out-Null
$ispcSource = Join-Path $SourceDir "bc7e.ispc"
$ispcObject = Join-Path $generatedDir "bc7e.obj"
$ispcHeader = Join-Path $generatedDir "bc7e_ispc.h"
& $ispc -g -O2 $ispcSource -o $ispcObject -h $ispcHeader "--target=sse2,sse4,avx,avx2" "--opt=fast-math" "--opt=disable-assertions"
if ($LASTEXITCODE -ne 0) { throw "ispc build failed" }

$wrapperProject = Join-Path $repoRoot "third_party\bc7e\wrapper\ds2_jacket_bc7e.vcxproj"
& $msbuild $wrapperProject "/p:Configuration=Release;Platform=x64;Bc7eGeneratedDir=$generatedDir" /m /nologo /v:q
if ($LASTEXITCODE -ne 0) { throw "wrapper msbuild failed" }

$builtDll = Join-Path $repoRoot "build\bc7e\wrapper-msbuild\Release\ds2_jacket_bc7e.dll"
if (-not (Test-Path $builtDll)) { throw "wrapper DLL not found: $builtDll" }
& (Join-Path $repoRoot "tools\verify-bc7e.ps1") -DllPath $builtDll
if ($LASTEXITCODE -ne 0) { throw "BC7E verify failed" }

New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
$installedDll = Join-Path $InstallDir "ds2_jacket_bc7e.dll"
Copy-Item -LiteralPath $builtDll -Destination $installedDll -Force
$hash = Get-FileHash -LiteralPath $installedDll -Algorithm SHA256
$meta = Join-Path $InstallDir "ds2_jacket_bc7e.txt"
@(
    "upstream=richgel999/bc7enc_rdo",
    "commit=$actualCommit",
    "ispc=$ispc",
    "sha256=$($hash.Hash)"
) | Set-Content -Encoding UTF8 -LiteralPath $meta

Write-Host "BC7E_DLL_BUILT $($hash.Hash) $installedDll"

if (-not [string]::IsNullOrWhiteSpace($GameScriptsDir)) {
    New-Item -ItemType Directory -Path $GameScriptsDir -Force | Out-Null
    $gameDll = Join-Path $GameScriptsDir "ds2_jacket_bc7e.dll"
    Copy-Item -LiteralPath $installedDll -Destination $gameDll -Force
    & (Join-Path $repoRoot "tools\verify-bc7e.ps1") -DllPath $gameDll
    if ($LASTEXITCODE -ne 0) { throw "game BC7E verify failed" }
    Write-Host "BC7E_DLL_INSTALLED $gameDll"
}
