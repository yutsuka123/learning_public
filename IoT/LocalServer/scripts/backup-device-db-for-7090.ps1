<#
.SYNOPSIS
LocalServer の device_db 復旧用スナップショットを退避する。

.DESCRIPTION
[重要] `data/settings.json`、`data/securityState.json`、`data/keyStore.json`、`data/wrapped_secret.bin`、
`data/wrapped_k_user.bin` を、復旧専用のスナップショットとしてまとめて退避する。
理由: `device_db` を単一ファイルにせず、LocalServer の復旧に必要な最小集合として保存するため。
[厳守] 秘密値そのものをメモへ平文で転記しない。
[厳守] 退避先は `data/secure-backups` 配下とし、復元時に同じ構成を戻せるようにする。

.PARAMETER InstallRoot
LocalServer のインストールルート。未指定時はスクリプトの親の親を使う。

.PARAMETER BackupRoot
退避先のルートディレクトリ。未指定時は `InstallRoot\data\secure-backups` を使う。

.EXAMPLE
.\backup-device-db-for-7090.ps1
.\backup-device-db-for-7090.ps1 -InstallRoot "C:\mydata\project\myproject\learning_public\IoT\LocalServer"
#>

param(
  [string]$InstallRoot = "",
  [string]$BackupRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$defaultRoot = Resolve-Path (Join-Path $scriptDir "..")
$InstallRoot = if ($InstallRoot) { Resolve-Path $InstallRoot } else { $defaultRoot }

$resolvedBackupRoot = if ($BackupRoot) {
  Resolve-Path $BackupRoot
} else {
  Join-Path $InstallRoot "data\secure-backups"
}

$dataDir = Join-Path $InstallRoot "data"
if (-not (Test-Path $dataDir)) {
  Write-Error "LocalServer data directory is not found. path=$dataDir"
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backupDir = Join-Path $resolvedBackupRoot "device-db-backup-$timestamp"
New-Item -ItemType Directory -Path $backupDir -Force | Out-Null

$sourceFiles = @(
  @{ Name = "settings.json"; Required = $true },
  @{ Name = "securityState.json"; Required = $true },
  @{ Name = "keyStore.json"; Required = $true },
  @{ Name = "wrapped_secret.bin"; Required = $false },
  @{ Name = "wrapped_k_user.bin"; Required = $false }
)

$manifestItems = @()
foreach ($fileSpec in $sourceFiles) {
  $sourcePath = Join-Path $dataDir $fileSpec.Name
  $destinationPath = Join-Path $backupDir $fileSpec.Name

  if (Test-Path $sourcePath) {
    Copy-Item -Path $sourcePath -Destination $destinationPath -Force
    $fileInfo = Get-Item $sourcePath
    $manifestItems += [PSCustomObject]@{
      fileName  = $fileSpec.Name
      copied    = $true
      required  = $fileSpec.Required
      sizeBytes = $fileInfo.Length
    }
  } elseif ($fileSpec.Required) {
    Write-Error "Required device_db file is not found. path=$sourcePath"
  } else {
    Write-Warning "Optional device_db file is not found. path=$sourcePath"
    $manifestItems += [PSCustomObject]@{
      fileName  = $fileSpec.Name
      copied    = $false
      required  = $false
      sizeBytes = 0
    }
  }
}

$manifest = [PSCustomObject]@{
  backupType = "device-db-snapshot"
  version    = 1
  createdAt  = (Get-Date).ToString("o")
  installRoot = $InstallRoot
  sourceDataDirectory = $dataDir
  files = $manifestItems
}

$manifestPath = Join-Path $backupDir "backup-manifest.json"
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Path $manifestPath -Encoding UTF8

$memoText = @"
# 復旧メモ ($timestamp)

- [重要] 目的: LocalServer の `device_db` を復旧可能な形で退避する。
- [厳守] このメモには機密値そのものを記載しない。
- [重要] バックアップ対象:
$(($manifestItems | ForEach-Object { "  - $($_.fileName) (copied=$($_.copied), required=$($_.required), sizeBytes=$($_.sizeBytes))" }) -join "`r`n")
- [重要] 復元時は `restore-device-db-after-7090.ps1` を使う。
- [推奨] 復元後は `settings.json`、`securityState.json`、`keyStore.json`、`wrapped_secret.bin`、`wrapped_k_user.bin` の整合を確認する。
- [禁止] 退避先を通常のサポートパッケージと混在させない。
"@

$memoPath = Join-Path $backupDir "restore-memo-device-db-$timestamp.md"
$memoText | Set-Content -Path $memoPath -Encoding UTF8

Write-Host "[OK] device_db snapshot saved: $backupDir"
Write-Host "[note] manifest: $manifestPath"
Write-Host "[note] memo: $memoPath"
Write-Host "[important] Treat the backup directory as sensitive data and securely delete it after recovery."
