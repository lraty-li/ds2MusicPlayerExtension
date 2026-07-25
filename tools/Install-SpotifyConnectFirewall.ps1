[CmdletBinding()]
param(
    [string]$GameRoot = "",
    [switch]$Remove
)

$ErrorActionPreference = "Stop"
$rulePrefix = "DS2MusicPlayer Spotify Connect"
$defaultGameRoot = "F:\SteamLibrary\steamapps\common\DEATH STRANDING 2 - ON THE BEACH"
$language = [Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName
$messages = if ($language -eq "zh") {
    @{
        GameRootPrompt = "请输入包含 DS2.exe 的游戏目录"
        MissingExe = "未在 '{0}' 找到 DS2.exe。"
        Removed = "已移除: {0}"
        Added = "已添加: {0}"
        RemoveComplete = "Spotify Connect 防火墙规则已移除。"
        InstallComplete = "Spotify Connect 防火墙规则已安装，无需重启游戏。"
    }
} else {
    @{
        GameRootPrompt = "Enter the game directory containing DS2.exe"
        MissingExe = "DS2.exe was not found in '{0}'."
        Removed = "Removed: {0}"
        Added = "Added: {0}"
        RemoveComplete = "Spotify Connect firewall rules were removed."
        InstallComplete = "Spotify Connect firewall rules were installed. No game restart is required."
    }
}

function Get-Text {
    param([string]$Key, [object[]]$Values)

    $text = $messages[$Key]
    if ($Values.Count -eq 0) {
        return $text
    }
    return [string]::Format($text, $Values)
}

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Resolve-GameRoot {
    param([string]$RequestedRoot)

    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        return (Resolve-Path -LiteralPath $RequestedRoot -ErrorAction Stop).Path
    }
    if (Test-Path -LiteralPath (Join-Path $defaultGameRoot "DS2.exe")) {
        return $defaultGameRoot
    }
    $enteredRoot = Read-Host (Get-Text "GameRootPrompt")
    return (Resolve-Path -LiteralPath $enteredRoot -ErrorAction Stop).Path
}

$resolvedGameRoot = Resolve-GameRoot $GameRoot
$gameExe = Join-Path $resolvedGameRoot "DS2.exe"
if (-not (Test-Path -LiteralPath $gameExe)) {
    throw (Get-Text "MissingExe" @($resolvedGameRoot))
}

if (-not (Test-Administrator)) {
    $quotedScript = '"{0}"' -f $PSCommandPath.Replace('"', '""')
    $quotedRoot = '"{0}"' -f $resolvedGameRoot.Replace('"', '""')
    $elevationArguments = "-NoProfile -ExecutionPolicy Bypass -File $quotedScript -GameRoot $quotedRoot"
    if ($Remove) {
        $elevationArguments += " -Remove"
    }
    $elevated = Start-Process `
        -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" `
        -Verb RunAs `
        -ArgumentList $elevationArguments `
        -Wait `
        -PassThru
    exit $elevated.ExitCode
}

$rules = @(
    @{ Name = "$rulePrefix TCP"; Protocol = "TCP"; Port = 57621 },
    @{ Name = "$rulePrefix mDNS"; Protocol = "UDP"; Port = 5353 }
)

foreach ($rule in $rules) {
    $existing = Get-NetFirewallRule -DisplayName $rule.Name -ErrorAction SilentlyContinue
    if ($existing) {
        $existing | Remove-NetFirewallRule
    }
    if ($Remove) {
        Write-Host (Get-Text "Removed" @($rule.Name))
        continue
    }
    New-NetFirewallRule `
        -DisplayName $rule.Name `
        -Direction Inbound `
        -Action Allow `
        -Profile Private `
        -Program $gameExe `
        -Protocol $rule.Protocol `
        -LocalPort $rule.Port `
        -Enabled True | Out-Null
    Write-Host (Get-Text "Added" @($rule.Name))
}

if ($Remove) {
    Write-Host (Get-Text "RemoveComplete")
} else {
    Write-Host (Get-Text "InstallComplete")
}
