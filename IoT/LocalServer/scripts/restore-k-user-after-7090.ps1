<#
.SYNOPSIS
7090 安全消去試験の後に、k-user バックアップ用ファイルを復元する。

.DESCRIPTION
[重要] keyStore.json（k-user のバックアップ用ファイル。生の k-user ではない）を data/ へ復元する。
理由: k-user 再発行を不要にし、既存デバイスとの暗号通信を継続するため。
[厳守] LocalServer プロセスは停止した状態で実行すること。
理由: ファイル上書き中の競合を防ぐため。

.PARAMETER InstallRoot
LocalServer のインストールルート（未指定時はスクリプトの親の親）。

.PARAMETER BackupDir
復元元のバックアップディレクトリ（未指定時は最新のバックアップを使用）。

.EXAMPLE
.\restore-k-user-after-7090.ps1
.\restore-k-user-after-7090.ps1 -BackupDir "%LOCALAPPDATA%\IoT-LocalServer-Backup-7090\20260315-120000"
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

$backupBaseDir = Join-Path $env:LOCALAPPDATA "IoT-LocalServer-Backup-7090"

if ($BackupDir) {
  $sourceDir = Resolve-Path $BackupDir
} else {
  if (-not (Test-Path $backupBaseDir)) {
    Write-Error "Backup base directory is not found. path=$backupBaseDir"
  }
  $latestDir = Get-ChildItem $backupBaseDir -Directory | Sort-Object Name -Descending | Select-Object -First 1
  if (-not $latestDir) {
    Write-Error "Backup base directory is empty. path=$backupBaseDir"
  }
  $sourceDir = $latestDir.FullName
}

$keyStoreBackup = Join-Path $sourceDir "keyStore.json"
if (-not (Test-Path $keyStoreBackup)) {
  Write-Error "keyStore.json is not found in the backup directory. path=$keyStoreBackup"
}

$dataDir = Join-Path $InstallRoot "data"
if (-not (Test-Path $dataDir)) {
  New-Item -ItemType Directory -Path $dataDir -Force | Out-Null
}

Copy-Item -Path $keyStoreBackup -Destination (Join-Path $dataDir "keyStore.json") -Force
Write-Host "[OK] k-user backup file restored: $dataDir\keyStore.json"
Write-Host "[note] Start LocalServer and verify the k-user state."
Write-Host "[important] Securely delete the backup directory when it is no longer needed."
