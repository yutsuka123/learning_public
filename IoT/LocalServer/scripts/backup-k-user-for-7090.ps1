<#
.SYNOPSIS
7090 安全消去試験の事前に、k-user バックアップ用ファイルを退避する。

.DESCRIPTION
[重要] 生の k-user ではなく、暗号化済み keyStore.json（k-user のバックアップ用ファイル）をコピーする。
理由: 試験後復元時に k-user 再発行を不要にするため。鍵管理設計のバックアップ方針に準拠。
[厳守] バックアップ先は LocalServer インストールルート外（%LOCALAPPDATA%\IoT-LocalServer-Backup-7090）とする。
理由: アンインストーラが data/ を消去するため、同一ドライブ内の外部パスへ退避する必要がある。

.PARAMETER InstallRoot
LocalServer のインストールルート（未指定時はスクリプトの親の親）。

.EXAMPLE
.\backup-k-user-for-7090.ps1
.\backup-k-user-for-7090.ps1 -InstallRoot "C:\IoT\LocalServer"
#>

param(
  [string]$InstallRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$defaultRoot = Resolve-Path (Join-Path $scriptDir "..")
$InstallRoot = if ($InstallRoot) { Resolve-Path $InstallRoot } else { $defaultRoot }

$backupBaseDir = Join-Path $env:LOCALAPPDATA "IoT-LocalServer-Backup-7090"
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backupDir = Join-Path $backupBaseDir $timestamp

# k-user バックアップ用ファイル: keyStore.json（暗号化済み。生の k-user ではない）
$keyStorePath = Join-Path $InstallRoot "data\keyStore.json"

# [推奨] バックアップ時は LocalServer を停止しておくこと。理由: コピー中の書き込み競合を防ぐため。
if (-not (Test-Path $keyStorePath)) {
  Write-Warning "keyStore.json is not found. k-user may not be issued yet. path=$keyStorePath"
  Write-Host "Press any key to continue."
  $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
  exit 0
}

New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
Copy-Item -Path $keyStorePath -Destination (Join-Path $backupDir "keyStore.json") -Force
Write-Host "[OK] k-user backup file saved: $backupDir\keyStore.json"
Write-Host "[note] After 7090, restore with restore-k-user-after-7090.ps1 or copy it back to data\keyStore.json manually."
Write-Host "[important] Treat the backup directory as sensitive data and securely delete it after the work is complete."
