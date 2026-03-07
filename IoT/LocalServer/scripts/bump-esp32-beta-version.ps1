<#
  [重要] ESP32書込み前の版数更新を自動化するスクリプト。
  概要:
    - `IoT/ESP32/header/version.h` の `kFirmwareVersion` を更新する。
    - `IoT/LocalServer/.env` の `OTA_FIRMWARE_VERSION` を同じ値へ同期する。
  主な仕様:
    - `-NextVersion` 未指定時は `x.y.z-beta.N` の `N` を +1 する。
    - `-NextVersion` 指定時は指定値を検証してその値を適用する。
    - `-DryRun` 指定時はファイルを書き換えずに結果だけ表示する。
  制限事項:
    - 対応版数形式は `x.y.z-beta.N` のみ。
    - `version.h` と `.env` の該当キーが存在しない場合は失敗終了する。
#>

param(
  [string]$nextVersion = "",
  [switch]$dryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-InfoMessage {
  param(
    [string]$message
  )
  Write-Host "[INFO] $message"
}

function Parse-BetaVersion {
  param(
    [string]$versionText
  )
  $versionPattern = '^(?<prefix>\d+\.\d+\.\d+-beta\.)(?<betaNumber>\d+)$'
  $matchResult = [regex]::Match($versionText, $versionPattern)
  if (-not $matchResult.Success) {
    throw "Parse-BetaVersion failed. versionText='$versionText' は 'x.y.z-beta.N' 形式ではありません。"
  }
  return @{
    prefix = $matchResult.Groups["prefix"].Value
    betaNumber = [int]$matchResult.Groups["betaNumber"].Value
  }
}

function Resolve-NextVersion {
  param(
    [string]$currentVersionText,
    [string]$requestedVersionText
  )
  if ([string]::IsNullOrWhiteSpace($requestedVersionText)) {
    $parsedVersion = Parse-BetaVersion -versionText $currentVersionText
    $nextBetaNumber = $parsedVersion.betaNumber + 1
    return "$($parsedVersion.prefix)$nextBetaNumber"
  }

  $null = Parse-BetaVersion -versionText $requestedVersionText
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
    throw "Update-VersionHeader failed. kFirmwareVersion が見つかりません。path='$versionHeaderPath'"
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
    Set-Content -Path $versionHeaderPath -Value $updatedHeaderText -Encoding UTF8
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
    throw "Update-LocalServerEnv failed. OTA_FIRMWARE_VERSION が見つかりません。path='$localServerEnvPath'"
  }

  $updatedEnvText = [regex]::Replace(
    $envText,
    $envPattern,
    ('OTA_FIRMWARE_VERSION={0}' -f $newVersionText),
    [System.Text.RegularExpressions.RegexOptions]::None,
    [TimeSpan]::FromSeconds(2)
  )

  if (-not $isDryRun) {
    Set-Content -Path $localServerEnvPath -Value $updatedEnvText -Encoding UTF8
  }
}

try {
  $scriptDirectoryPath = Split-Path -Parent $MyInvocation.MyCommand.Path
  $iotDirectoryPath = (Resolve-Path (Join-Path $scriptDirectoryPath "..\..")).Path
  $versionHeaderPath = Join-Path $iotDirectoryPath "ESP32\header\version.h"
  $localServerEnvPath = Join-Path $iotDirectoryPath "LocalServer\.env"

  if (-not (Test-Path -Path $versionHeaderPath -PathType Leaf)) {
    throw "version.h が存在しません。path='$versionHeaderPath'"
  }
  if (-not (Test-Path -Path $localServerEnvPath -PathType Leaf)) {
    throw ".env が存在しません。path='$localServerEnvPath'"
  }

  $currentVersionText = Update-VersionHeader -versionHeaderPath $versionHeaderPath -newVersionText "__TEMP__" -isDryRun $true
  $resolvedVersionText = Resolve-NextVersion -currentVersionText $currentVersionText -requestedVersionText $nextVersion

  $null = Update-VersionHeader -versionHeaderPath $versionHeaderPath -newVersionText $resolvedVersionText -isDryRun $dryRun
  Update-LocalServerEnv -localServerEnvPath $localServerEnvPath -newVersionText $resolvedVersionText -isDryRun $dryRun

  if ($dryRun) {
    Write-InfoMessage "DryRun: version は更新していません。"
  }
  Write-InfoMessage "version updated: $currentVersionText -> $resolvedVersionText"
  Write-InfoMessage "updated files:"
  Write-InfoMessage "  - $versionHeaderPath"
  Write-InfoMessage "  - $localServerEnvPath"
}
catch {
  Write-Error "bump-esp32-beta-version.ps1 failed. message=$($_.Exception.Message)"
  exit 1
}
