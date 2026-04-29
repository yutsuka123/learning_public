<#
.SYNOPSIS
LocalServer の device_db 復旧用スナップショットを復元する。

.DESCRIPTION
[重要] `data/secure-backups` 配下の device_db スナップショットから、
`data/settings.json`、`data/securityState.json`、`data/keyStore.json`、
`data/wrapped_secret.bin`、`data/wrapped_k_user.bin` を復元する。
理由: 同一PC再インストール後に、LocalServer の管理状態と鍵状態をまとめて戻すため。
[厳守] LocalServer プロセス停止後に実行する。
[厳守] 復元元が複数ある場合は、明示指定した `BackupDir` を優先する。

.PARAMETER InstallRoot
LocalServer のインストールルート。未指定時はスクリプトの親の親を使う。

.PARAMETER BackupDir
復元元のスナップショットディレクトリ。未指定時は `data/secure-backups` 配下の最新を使う。

.EXAMPLE
.\restore-device-db-after-7090.ps1
.\restore-device-db-after-7090.ps1 -BackupDir "C:\mydata\project\myproject\learning_public\IoT\LocalServer\data\secure-backups\device-db-backup-20260424-120000"
#>

param(
  [string]$InstallRoot = "",
  [string]$BackupDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$defaultRoot = Resolve-Path (Join-Path $scriptDir "..")
$InstallRoot = if ($InstallRoot) { Resolve-Path $InstallRoot } else { $defaultRoot }

$backupRoot = if ($BackupDir) {
  Resolve-Path $BackupDir
} else {
  $candidateRoot = Join-Path $InstallRoot "data\secure-backups"
  if (-not (Test-Path $candidateRoot)) {
    Write-Error "Backup root directory is not found. path=$candidateRoot"
  }
  $latestSnapshot = Get-ChildItem $candidateRoot -Directory |
    Where-Object { $_.Name -like "device-db-backup-*" } |
    Sort-Object Name -Descending |
    Select-Object -First 1
  if (-not $latestSnapshot) {
    Write-Error "No device_db backup snapshot is found. path=$candidateRoot"
  }
  $latestSnapshot.FullName
}

$manifestPath = Join-Path $backupRoot "backup-manifest.json"
if (-not (Test-Path $manifestPath)) {
  Write-Warning "backup-manifest.json is not found. restore will proceed with file presence checks only. path=$manifestPath"
}

$dataDir = Join-Path $InstallRoot "data"
if (-not (Test-Path $dataDir)) {
  New-Item -ItemType Directory -Path $dataDir -Force | Out-Null
}

$restoreFiles = @(
  @{ Name = "settings.json"; Required = $true },
  @{ Name = "securityState.json"; Required = $true },
  @{ Name = "keyStore.json"; Required = $true },
  @{ Name = "wrapped_secret.bin"; Required = $false },
  @{ Name = "wrapped_k_user.bin"; Required = $false }
)

foreach ($fileSpec in $restoreFiles) {
  $backupFilePath = Join-Path $backupRoot $fileSpec.Name
  $destinationPath = Join-Path $dataDir $fileSpec.Name

  if (Test-Path $backupFilePath) {
    Copy-Item -Path $backupFilePath -Destination $destinationPath -Force
  } elseif ($fileSpec.Required) {
    Write-Error "Required device_db backup file is not found. path=$backupFilePath"
  } else {
    Write-Warning "Optional device_db backup file is not found. path=$backupFilePath"
  }
}

if (Test-Path $manifestPath) {
  Copy-Item -Path $manifestPath -Destination (Join-Path $dataDir "device-db-restore-manifest.json") -Force
}

Write-Host "[OK] device_db snapshot restored from: $backupRoot"
Write-Host "[note] Start LocalServer and verify the restored state."
Write-Host "[important] Re-check the installed device identity and backup memo before resuming test work."
