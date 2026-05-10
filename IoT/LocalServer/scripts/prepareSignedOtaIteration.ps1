<#
.SYNOPSIS
署名付き OTA 試験の各反復前準備を自動化する。

.DESCRIPTION
[重要] 本スクリプトは `7083` などの OTA 試験前に、版数更新、ESP32 ファームウェア再ビルド、
        および起動中 `LocalServer` の `otaFirmwareVersion` / `firmwareLocalPath` 同期を一括実行する。
[厳守] 版数は `IoT/ESP32/header/version.h` を正とし、`bump-esp32-beta-version.ps1` で一元更新する。
[厳守] `LocalServer` を再起動しない試験でも、`PUT /api/settings` でメモリ上の設定を同期する。
[禁止] 旧版数のまま OTA を開始し、`workflow timeout` や版数不一致を再発させない。
[制限] 起動中 `LocalServer` の `baseUrl` に到達できない場合は失敗終了する。
#>

param(
  [string]$baseUrl = "http://127.0.0.1:3100",
  [string]$nextVersion = "",
  [string]$pioEnvironment = "esp32s3_secure",
  [switch]$skipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-InfoMessage {
  param(
    [string]$message
  )

  Write-Host "[INFO] $message"
}

function Get-FirmwareVersionFromHeader {
  param(
    [string]$versionHeaderPath
  )

  $headerText = Get-Content -Path $versionHeaderPath -Raw -Encoding UTF8
  $headerMatch = [regex]::Match($headerText, 'kFirmwareVersion\s*=\s*"(?<version>[^"]+)"')
  if (-not $headerMatch.Success) {
    throw "Get-FirmwareVersionFromHeader failed. version text was not found. path='$versionHeaderPath'"
  }

  return $headerMatch.Groups["version"].Value
}

function Invoke-BumpVersion {
  param(
    [string]$bumpScriptPath,
    [string]$requestedVersionText
  )

  $commandArguments = @(
    "-ExecutionPolicy",
    "Bypass",
    "-File",
    $bumpScriptPath
  )
  if (-not [string]::IsNullOrWhiteSpace($requestedVersionText)) {
    $commandArguments += @(
      "-nextVersion",
      $requestedVersionText
    )
  }

  & powershell @commandArguments
  if ($LASTEXITCODE -ne 0) {
    throw "Invoke-BumpVersion failed. exitCode=$LASTEXITCODE bumpScriptPath='$bumpScriptPath'"
  }
}

function Invoke-Esp32Build {
  param(
    [string]$esp32DirectoryPath,
    [string]$targetEnvironment
  )

  Push-Location $esp32DirectoryPath
  try {
    & python -m platformio run -e $targetEnvironment
    if ($LASTEXITCODE -ne 0) {
      throw "Invoke-Esp32Build failed. exitCode=$LASTEXITCODE targetEnvironment='$targetEnvironment'"
    }
  }
  finally {
    Pop-Location
  }
}

function Update-LocalServerSettings {
  param(
    [string]$settingsApiBaseUrl,
    [string]$firmwareVersionText,
    [string]$firmwarePath
  )

  $requestBodyObject = @{
    firmwareSource = "localPath"
    firmwareLocalPath = $firmwarePath
    otaFirmwareVersion = $firmwareVersionText
  }
  $requestBodyJson = $requestBodyObject | ConvertTo-Json -Depth 5

  $responseObject = Invoke-RestMethod -Method Put -Uri "$settingsApiBaseUrl/api/settings" -ContentType "application/json" -Body $requestBodyJson
  if ($null -eq $responseObject -or $responseObject.result -ne "OK") {
    throw "Update-LocalServerSettings failed. baseUrl='$settingsApiBaseUrl' response='$($requestBodyJson)'"
  }

  return $responseObject
}

try {
  $scriptDirectoryPath = Split-Path -Parent $MyInvocation.MyCommand.Path
  $localServerDirectoryPath = (Resolve-Path (Join-Path $scriptDirectoryPath "..")).Path
  $iotDirectoryPath = (Resolve-Path (Join-Path $scriptDirectoryPath "..\..")).Path
  $esp32DirectoryPath = (Resolve-Path (Join-Path $iotDirectoryPath "ESP32")).Path
  $bumpScriptPath = Join-Path $scriptDirectoryPath "bump-esp32-beta-version.ps1"
  $versionHeaderPath = Join-Path $iotDirectoryPath "ESP32\header\version.h"

  if (-not (Test-Path -Path $bumpScriptPath -PathType Leaf)) {
    throw "prepareSignedOtaIteration.ps1 failed. bump script was not found. path='$bumpScriptPath'"
  }
  if (-not (Test-Path -Path $versionHeaderPath -PathType Leaf)) {
    throw "prepareSignedOtaIteration.ps1 failed. version header was not found. path='$versionHeaderPath'"
  }

  Write-InfoMessage "OTA iteration preparation started. baseUrl=$baseUrl pioEnvironment=$pioEnvironment skipBuild=$skipBuild"
  Invoke-BumpVersion -bumpScriptPath $bumpScriptPath -requestedVersionText $nextVersion

  $resolvedVersionText = Get-FirmwareVersionFromHeader -versionHeaderPath $versionHeaderPath
  $firmwarePath = Join-Path $iotDirectoryPath "ESP32\.pio\build\$pioEnvironment\firmware.bin"

  if (-not $skipBuild) {
    Invoke-Esp32Build -esp32DirectoryPath $esp32DirectoryPath -targetEnvironment $pioEnvironment
  }

  if (-not (Test-Path -Path $firmwarePath -PathType Leaf)) {
    throw "prepareSignedOtaIteration.ps1 failed. firmware.bin was not found after build. path='$firmwarePath'"
  }

  $updatedSettings = Update-LocalServerSettings -settingsApiBaseUrl $baseUrl -firmwareVersionText $resolvedVersionText -firmwarePath $firmwarePath

  Write-InfoMessage "OTA iteration preparation completed."
  Write-InfoMessage "resolvedVersion=$resolvedVersionText"
  Write-InfoMessage "firmwarePath=$firmwarePath"
  Write-InfoMessage "settings.otaFirmwareVersion=$($updatedSettings.settings.otaFirmwareVersion)"
  Write-InfoMessage "settings.firmwareLocalPath=$($updatedSettings.settings.firmwareLocalPath)"
}
catch {
  Write-Error "prepareSignedOtaIteration.ps1 failed. message=$($_.Exception.Message)"
  exit 1
}
