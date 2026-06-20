param(
    [Parameter(Mandatory = $true)]
    [string]$FilePath,
    [string]$ApiKey = $env:NEXUSMODS_API_KEY,
    [string]$ModFileId = $env:NEXUSMODS_FILE_ID,
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [string]$DisplayName = "",
    [string]$Description = "",
    [ValidateSet("main", "optional", "miscellaneous")]
    [string]$FileCategory = "main",
    [switch]$ArchiveExistingFile,
    [switch]$PrimaryModManagerDownload,
    [bool]$AllowModManagerDownload = $true,
    [switch]$ShowRequirementsPopUp,
    [string]$ApiBase = "https://api.nexusmods.com/v3"
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.Net.Http

if ([string]::IsNullOrWhiteSpace($ApiKey)) {
    throw "NEXUSMODS_API_KEY is required."
}
if ([string]::IsNullOrWhiteSpace($ModFileId)) {
    throw "NEXUSMODS_FILE_ID is required."
}
if (-not (Test-Path -LiteralPath $FilePath)) {
    throw "Package file not found: $FilePath"
}

$resolvedFile = (Resolve-Path -LiteralPath $FilePath).Path
$fileInfo = Get-Item -LiteralPath $resolvedFile
$fileName = $fileInfo.Name
if ([string]::IsNullOrWhiteSpace($DisplayName)) {
    $DisplayName = $fileName
}

$apiHeaders = @{
    "apikey" = $ApiKey
    "User-Agent" = "DS2MusicPlayer-release"
}

function Invoke-NexusJson {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet("GET", "POST")]
        [string]$Method,
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [object]$Body = $null
    )

    $uri = "$ApiBase$Path"
    $params = @{
        Uri = $uri
        Method = $Method
        Headers = $apiHeaders
    }
    if ($null -ne $Body) {
        $params.ContentType = "application/json"
        $params.Body = $Body | ConvertTo-Json -Depth 8
    }

    try {
        return Invoke-RestMethod @params
    } catch {
        $response = $_.Exception.Response
        if ($response -and $response.GetResponseStream()) {
            $reader = New-Object System.IO.StreamReader($response.GetResponseStream())
            $details = $reader.ReadToEnd()
            throw "Nexus Mods API request failed: $Method $Path`n$details"
        }
        throw
    }
}

function Read-FileChunk {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.FileStream]$Stream,
        [Parameter(Mandatory = $true)]
        [long]$Length
    )

    $buffer = New-Object byte[] ([int]$Length)
    $offset = 0
    while ($offset -lt $Length) {
        $read = $Stream.Read($buffer, $offset, [int]($Length - $offset))
        if ($read -le 0) {
            break
        }
        $offset += $read
    }
    if ($offset -eq $buffer.Length) {
        return $buffer
    }

    $shortBuffer = New-Object byte[] $offset
    [Array]::Copy($buffer, $shortBuffer, $offset)
    return $shortBuffer
}

function Send-PresignedPut {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Uri,
        [Parameter(Mandatory = $true)]
        [byte[]]$Content
    )

    $client = [System.Net.Http.HttpClient]::new()
    try {
        $byteContent = [System.Net.Http.ByteArrayContent]::new($Content)
        $byteContent.Headers.ContentType =
            [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse("application/octet-stream")
        $response = $client.PutAsync($Uri, $byteContent).GetAwaiter().GetResult()
        if (-not $response.IsSuccessStatusCode) {
            $details = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
            throw "Presigned upload failed: $([int]$response.StatusCode) $details"
        }

        $etagValues = $response.Headers.GetValues("ETag")
        return ($etagValues | Select-Object -First 1).Trim('"')
    } finally {
        $client.Dispose()
    }
}

function Send-CompleteMultipart {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Uri,
        [Parameter(Mandatory = $true)]
        [string]$Xml
    )

    $client = [System.Net.Http.HttpClient]::new()
    try {
        $content = [System.Net.Http.StringContent]::new($Xml)
        $content.Headers.ContentType =
            [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse("application/xml")
        $response = $client.PostAsync($Uri, $content).GetAwaiter().GetResult()
        if (-not $response.IsSuccessStatusCode) {
            $details = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
            throw "Complete multipart upload failed: $([int]$response.StatusCode) $details"
        }
    } finally {
        $client.Dispose()
    }
}

function Wait-NexusUploadAvailable {
    param(
        [Parameter(Mandatory = $true)]
        [string]$UploadId
    )

    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        $upload = Invoke-NexusJson -Method GET -Path "/uploads/$UploadId"
        if ($upload.data.state -eq "available") {
            return
        }
        Start-Sleep -Seconds ([Math]::Min(2 + $attempt, 30))
    }
    throw "Upload did not become available: $UploadId"
}

$createUpload = @{
    size_bytes = [Int64]$fileInfo.Length
    filename = $fileName
}
$upload = Invoke-NexusJson -Method POST -Path "/uploads/multipart" -Body $createUpload
$uploadId = $upload.data.id
$partSize = [Int64]$upload.data.part_size_bytes
$partUrls = @($upload.data.part_presigned_urls)
Write-Host "NEXUS_UPLOAD_CREATED $uploadId parts=$($partUrls.Count)"

$parts = New-Object System.Collections.Generic.List[object]
$stream = [System.IO.File]::OpenRead($resolvedFile)
try {
    for ($i = 0; $i -lt $partUrls.Count; $i++) {
        $remaining = $fileInfo.Length - $stream.Position
        $length = [Math]::Min($partSize, $remaining)
        $chunk = Read-FileChunk -Stream $stream -Length $length
        $etag = Send-PresignedPut -Uri $partUrls[$i] -Content $chunk
        $parts.Add([pscustomobject]@{
            PartNumber = $i + 1
            ETag = $etag
        })
        Write-Host "NEXUS_UPLOAD_PART $($i + 1)/$($partUrls.Count)"
    }
} finally {
    $stream.Dispose()
}

$xmlParts = foreach ($part in $parts) {
    $safeEtag = [System.Security.SecurityElement]::Escape($part.ETag)
    "  <Part><PartNumber>$($part.PartNumber)</PartNumber><ETag>$safeEtag</ETag></Part>"
}
$completeXml = "<CompleteMultipartUpload>`n$($xmlParts -join "`n")`n</CompleteMultipartUpload>"
Send-CompleteMultipart -Uri $upload.data.complete_presigned_url -Xml $completeXml

Invoke-NexusJson -Method POST -Path "/uploads/$uploadId/finalise" | Out-Null
Wait-NexusUploadAvailable -UploadId $uploadId

$createVersion = @{
    upload_id = $uploadId
    name = $DisplayName
    version = $Version
    file_category = $FileCategory
    archive_existing_file = [bool]$ArchiveExistingFile
    primary_mod_manager_download = [bool]$PrimaryModManagerDownload
    allow_mod_manager_download = $AllowModManagerDownload
    show_requirements_pop_up = [bool]$ShowRequirementsPopUp
}
if (-not [string]::IsNullOrWhiteSpace($Description)) {
    $createVersion.description = $Description
}

$versionResult = Invoke-NexusJson `
    -Method POST `
    -Path "/mod-files/$ModFileId/versions" `
    -Body $createVersion

$versionId = $versionResult.data.version.id
if ($env:GITHUB_OUTPUT) {
    "version_id=$versionId" | Add-Content -Path $env:GITHUB_OUTPUT -Encoding UTF8
}
Write-Host "NEXUS_UPLOAD_OK version_id=$versionId"
