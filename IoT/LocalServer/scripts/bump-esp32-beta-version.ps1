<#
  Update ESP32 beta version before rebuild.
  Targets:
    - IoT/ESP32/header/version.h
    - IoT/LocalServer/.env
    - IoT/LocalServer/data/settings.json (if exists)
  Rules:
    - If -NextVersion is omitted, increment x.y.z-beta.N by +1.
    - Only x.y.z-beta.N format is supported.
    - If -DryRun is used, do not write files.
#>

param(
  [string]$nextVersion = "",
  [switch]$dryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-InfoMessage {
  param([string]$message)
  Write-Host "[INFO] $message"
}

function Write-Utf8Text {
  param(
    [string]$path,
    [string]$text
  )
  $utf8Encoding = New-Object System.Text.UTF8Encoding($false)
  [System.IO.File]::WriteAllText($path, $text, $utf8Encoding)
}

function Get-BetaVersionInfo {
  param([string]$versionText)
  $versionPattern = '^(?<prefix>\d+\.\d+\.\d+-beta\.)(?<betaNumber>\d+)$'
  $matchResult = [regex]::Match($versionText, $versionPattern)
  if (-not $matchResult.Success) {
    throw "Get-BetaVersionInfo failed. versionText='$versionText' is not in x.y.z-beta.N format."
  }

  return @{
    prefix = $matchResult.Groups['prefix'].Value
    betaNumber = [int]$matchResult.Groups['betaNumber'].Value
  }
}

function Resolve-NextVersion {
  param(
    [string]$currentVersionText,
    [string]$requestedVersionText
  )
  if ([string]::IsNullOrWhiteSpace($requestedVersionText)) {
    $parsedVersion = Get-BetaVersionInfo -versionText $currentVersionText
    $nextBetaNumber = $parsedVersion.betaNumber + 1
    return "$($parsedVersion.prefix)$nextBetaNumber"
  }

  $null = Get-BetaVersionInfo -versionText $requestedVersionText
  return $requestedVersionText
}

function Update-VersionHeader {
  param(
    [string]$versionHeaderPath,
    [string]$newVersionText,
    [bool]$isDryRun
  )
  $headerText = Get-Content -Path $versionHeaderPath -Raw -Encoding UTF8
  $headerPattern = 'kFirmwareVersion\s*=\s*"(?<version>[^"]+)"'
  $headerMatch = [regex]::Match($headerText, $headerPattern)
  if (-not $headerMatch.Success) {
    throw "Update-VersionHeader failed. kFirmwareVersion was not found. path='$versionHeaderPath'"
  }

  $currentVersionText = $headerMatch.Groups["version"].Value
  $updatedHeaderText = [regex]::Replace(
    $headerText,
    $headerPattern,
    ('kFirmwareVersion = "{0}"' -f $newVersionText),
    [System.Text.RegularExpressions.RegexOptions]::None,
    [TimeSpan]::FromSeconds(2)
  )

  if (-not $isDryRun) {
    Write-Utf8Text -path $versionHeaderPath -text $updatedHeaderText
  }

  return $currentVersionText
}

function Update-LocalServerEnv {
  param(
    [string]$localServerEnvPath,
    [string]$newVersionText,
    [bool]$isDryRun
  )
  $envText = Get-Content -Path $localServerEnvPath -Raw -Encoding UTF8
  $envPattern = '(?m)^OTA_FIRMWARE_VERSION=.*$'
  $envMatch = [regex]::Match($envText, $envPattern)
  if (-not $envMatch.Success) {
    throw "Update-LocalServerEnv failed. OTA_FIRMWARE_VERSION was not found. path='$localServerEnvPath'"
  }

  $updatedEnvText = [regex]::Replace(
    $envText,
    $envPattern,
    ('OTA_FIRMWARE_VERSION={0}' -f $newVersionText),
    [System.Text.RegularExpressions.RegexOptions]::None,
    [TimeSpan]::FromSeconds(2)
  )

  if (-not $isDryRun) {
    Write-Utf8Text -path $localServerEnvPath -text $updatedEnvText
  }
}

function Update-LocalServerSettingsJson {
  param(
    [string]$settingsJsonPath,
    [string]$newVersionText,
    [bool]$isDryRun
  )

  if (-not (Test-Path -Path $settingsJsonPath -PathType Leaf)) {
    Write-InfoMessage "Skip settings.json update because the file does not exist. path='$settingsJsonPath'"
    return
  }

  $settingsText = Get-Content -Path $settingsJsonPath -Raw -Encoding UTF8
  $settingsObject = $settingsText | ConvertFrom-Json
  if ($null -eq $settingsObject.PSObject.Properties['otaFirmwareVersion']) {
    throw "Update-LocalServerSettingsJson failed. otaFirmwareVersion was not found. path='$settingsJsonPath'"
  }

  $settingsObject.otaFirmwareVersion = $newVersionText
  $updatedSettingsText = $settingsObject | ConvertTo-Json -Depth 10
  if (-not $updatedSettingsText.EndsWith("`n")) {
    $updatedSettingsText += "`n"
  }

  if (-not $isDryRun) {
    Write-Utf8Text -path $settingsJsonPath -text $updatedSettingsText
  }
}

try {
  $scriptDirectoryPath = Split-Path -Parent $MyInvocation.MyCommand.Path
  $iotDirectoryPath = (Resolve-Path (Join-Path $scriptDirectoryPath "..\..")).Path
  $versionHeaderPath = Join-Path $iotDirectoryPath "ESP32\header\version.h"
  $localServerEnvPath = Join-Path $iotDirectoryPath "LocalServer\.env"
  $localServerSettingsJsonPath = Join-Path $iotDirectoryPath "LocalServer\data\settings.json"

  if (-not (Test-Path -Path $versionHeaderPath -PathType Leaf)) {
    throw "version.h does not exist. path='$versionHeaderPath'"
  }
  if (-not (Test-Path -Path $localServerEnvPath -PathType Leaf)) {
    throw ".env does not exist. path='$localServerEnvPath'"
  }

  $currentVersionText = Update-VersionHeader -versionHeaderPath $versionHeaderPath -newVersionText "__TEMP__" -isDryRun $true
  $resolvedVersionText = Resolve-NextVersion -currentVersionText $currentVersionText -requestedVersionText $nextVersion

  $null = Update-VersionHeader -versionHeaderPath $versionHeaderPath -newVersionText $resolvedVersionText -isDryRun $dryRun
  Update-LocalServerEnv -localServerEnvPath $localServerEnvPath -newVersionText $resolvedVersionText -isDryRun $dryRun
  Update-LocalServerSettingsJson -settingsJsonPath $localServerSettingsJsonPath -newVersionText $resolvedVersionText -isDryRun $dryRun

  if ($dryRun) {
    Write-InfoMessage "DryRun: files were not modified."
  }
  Write-InfoMessage "version updated: $currentVersionText -> $resolvedVersionText"
  Write-InfoMessage "updated files:"
  Write-InfoMessage "  - $versionHeaderPath"
  Write-InfoMessage "  - $localServerEnvPath"
  Write-InfoMessage "  - $localServerSettingsJsonPath"
}
catch {
  Write-Error "bump-esp32-beta-version.ps1 failed. message=$($_.Exception.Message)"
  exit 1
}
