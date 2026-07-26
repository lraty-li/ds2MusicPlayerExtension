$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

$requiredFiles = @(
    "index.html",
    "style.css",
    "auth.js",
    "app.js",
    "start.ps1",
    "package.json",
    "capture-extension\manifest.json",
    "capture-extension\service_worker.js",
    "capture-extension\offscreen.html",
    "capture-extension\offscreen.js",
    "capture-extension\capture-metrics-worklet.js"
)

foreach ($relativePath in $requiredFiles) {
    $path = Join-Path $root $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing required file: $relativePath"
    }
}

$manifestPath = Join-Path $root "capture-extension\manifest.json"
$manifest = Get-Content -Raw -Encoding UTF8 $manifestPath | ConvertFrom-Json
if ($manifest.manifest_version -ne 3) {
    throw "capture-extension must use Manifest V3"
}
if ([int]$manifest.minimum_chrome_version -lt 116) {
    throw "capture-extension requires Chrome 116 or newer"
}

$codeRelativePaths = @(
    "index.html",
    "style.css",
    "auth.js",
    "app.js",
    "start.ps1",
    "build.ps1",
    "capture-extension\service_worker.js",
    "capture-extension\offscreen.html",
    "capture-extension\offscreen.js",
    "capture-extension\capture-metrics-worklet.js"
)
$codeFiles = @($codeRelativePaths | ForEach-Object {
    Get-Item -LiteralPath (Join-Path $root $_)
})

foreach ($file in $codeFiles) {
    $lineCount = @(Get-Content -Encoding UTF8 -LiteralPath $file.FullName).Count
    if ($lineCount -gt 300) {
        throw "Code file exceeds 300 lines: $($file.FullName) ($lineCount)"
    }
}

$node = Get-Command node -ErrorAction Stop
$javascriptFiles = $codeFiles | Where-Object { $_.Extension -eq ".js" }
foreach ($file in $javascriptFiles) {
    & $node.Source --check $file.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "JavaScript syntax check failed: $($file.FullName)"
    }
}

Write-Host "Static validation passed."
Write-Host "Checked $($codeFiles.Count) code files; all are at most 300 lines."
