<#
.SYNOPSIS
LocalServer をアンインストールし、機密データを安全消去する。

.DESCRIPTION
[003-0015][厳守] 安全消去ポリシー.md 4.1 に従い、機密ファイルは上書き消去後に削除する。
[重要] 消去成否の監査ログを %LOCALAPPDATA%\IoT-LocalServer-Uninstall\ に残す。
[禁止] 通常削除のみで機密残留を許容しない。

.PARAMETER InstallRoot
LocalServer のインストールルート（未指定時はスクリプトの親の親 = LocalServer ルート）。

.PARAMETER NoConfirm
確認プロンプトをスキップする（非対話用）。

.EXAMPLE
.\uninstall-local-server.ps1
.\uninstall-local-server.ps1 -InstallRoot "C:\IoT\LocalServer" -NoConfirm
#>

param(
  [string]$InstallRoot = "",
  [switch]$NoConfirm
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$defaultRoot = Resolve-Path (Join-Path $scriptDir "..")
$InstallRoot = if ($InstallRoot) { Resolve-Path $InstallRoot } else { $defaultRoot }

$taskName = "IoT_LocalServer_AutoStart"
$auditBaseDir = Join-Path $env:LOCALAPPDATA "IoT-LocalServer-Uninstall"
$auditFileName = "audit-{0:yyyyMMdd-HHmmss}.json" -f (Get-Date)
$auditFilePath = Join-Path $auditBaseDir $auditFileName

$script:auditEntries = @()
function Add-AuditEntry {
  param(
    [string]$Path,
    [string]$Kind,
    [string]$Result,
    [string]$Detail = ""
  )
  $script:auditEntries += [PSCustomObject]@{
    path   = $Path
    kind   = $Kind
    result = $Result
    detail = $Detail
    at     = (Get-Date).ToString("o")
  }
}

function Write-AuditLog {
  if (-not (Test-Path $auditBaseDir)) {
    New-Item -ItemType Directory -Path $auditBaseDir -Force | Out-Null
  }
  $report = @{
    uninstalledAt = (Get-Date).ToString("o")
    installRoot   = $InstallRoot
    executedBy    = $env:USERNAME
    entries       = $script:auditEntries
  }
  $report | ConvertTo-Json -Depth 5 | Set-Content -Path $auditFilePath -Encoding UTF8
  Write-Host "[監査] ログ保存: $auditFilePath"
}

function Secure-DeleteFile {
  param([string]$FilePath)
  if (-not (Test-Path $FilePath)) {
    Add-AuditEntry -Path $FilePath -Kind "secure-delete" -Result "skip" -Detail "not found"
    return
  }
  try {
    $fi = Get-Item $FilePath -Force
    if ($fi.PSIsContainer) { return }
    $len = $fi.Length
    $bytes = New-Object byte[] $len
    [System.Security.Cryptography.RandomNumberGenerator]::Fill($bytes)
    [System.IO.File]::WriteAllBytes($FilePath, $bytes)
    Remove-Item $FilePath -Force
    Add-AuditEntry -Path $FilePath -Kind "secure-delete" -Result "ok" -Detail "overwrite+delete bytes=$len"
  } catch {
    Add-AuditEntry -Path $FilePath -Kind "secure-delete" -Result "fail" -Detail $_.Exception.Message
    throw
  }
}

function Stop-LocalServerProcess {
  $allNode = Get-Process -Name "node" -ErrorAction SilentlyContinue
  foreach ($p in $allNode) {
    try {
      $cim = Get-CimInstance Win32_Process -Filter "ProcessId=$($p.Id)" -ErrorAction SilentlyContinue
      $cmd = if ($cim) { $cim.CommandLine } else { "" }
      if ($cmd -like "*$InstallRoot*" -or $cmd -like "*dist\server.js*") {
        $p | Stop-Process -Force -ErrorAction Stop
        Add-AuditEntry -Path "process" -Kind "stop" -Result "ok" -Detail "pid=$($p.Id)"
      }
    } catch {
      Add-AuditEntry -Path "process" -Kind "stop" -Result "fail" -Detail "pid=$($p.Id) $($_.Exception.Message)"
    }
  }
}

if (-not $NoConfirm) {
  $msg = "LocalServer をアンインストールします。機密データは上書き消去されます。インストールルート: $InstallRoot"
  $r = Read-Host "$msg`n続行しますか? (y/N)"
  if ($r -notmatch '^[yY]') { exit 0 }
}

Add-AuditEntry -Path "uninstall" -Kind "start" -Result "ok" -Detail "installRoot=$InstallRoot"

# 1. プロセス停止
Stop-LocalServerProcess
Start-Sleep -Seconds 2

# 2. Task Scheduler 解除
if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
  try {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    Add-AuditEntry -Path "scheduled-task" -Kind "unregister" -Result "ok" -Detail $taskName
  } catch {
    Add-AuditEntry -Path "scheduled-task" -Kind "unregister" -Result "fail" -Detail $_.Exception.Message
  }
}

# 3. 機密ファイル上書き消去（安全消去ポリシー 4.1）
$secureFiles = @(
  (Join-Path $InstallRoot "data\keyStore.json"),
  (Join-Path $InstallRoot "data\securityState.json"),
  (Join-Path $InstallRoot "data\settings.json")
)
foreach ($f in $secureFiles) {
  try { Secure-DeleteFile -FilePath $f } catch { Write-Warning "Secure-Delete failed: $f $_" }
}

$logsDir = Join-Path $InstallRoot "logs"
if (Test-Path $logsDir) {
  Get-ChildItem $logsDir -File -Filter "*.log" -ErrorAction SilentlyContinue | ForEach-Object {
    try { Secure-DeleteFile -FilePath $_.FullName } catch { Write-Warning "Secure-Delete failed: $($_.FullName) $_" }
  }
}

$uploadsDir = Join-Path $InstallRoot "uploads"
if (Test-Path $uploadsDir) {
  Get-ChildItem $uploadsDir -File -Recurse -ErrorAction SilentlyContinue | ForEach-Object {
    try { Secure-DeleteFile -FilePath $_.FullName } catch { Write-Warning "Secure-Delete failed: $($_.FullName) $_" }
  }
}

$certsDir = Join-Path $InstallRoot "certs"
if (Test-Path $certsDir) {
  Get-ChildItem $certsDir -File -ErrorAction SilentlyContinue | ForEach-Object {
    try { Secure-DeleteFile -FilePath $_.FullName } catch { Write-Warning "Secure-Delete failed: $($_.FullName) $_" }
  }
}

# 4. 残余ディレクトリの通常削除
$dirsToRemove = @("data", "logs", "uploads")
foreach ($d in $dirsToRemove) {
  $p = Join-Path $InstallRoot $d
  if (Test-Path $p) {
    try {
      Remove-Item $p -Recurse -Force -ErrorAction Stop
      Add-AuditEntry -Path $p -Kind "remove-dir" -Result "ok"
    } catch {
      Add-AuditEntry -Path $p -Kind "remove-dir" -Result "fail" -Detail $_.Exception.Message
    }
  }
}

Add-AuditEntry -Path "uninstall" -Kind "finish" -Result "ok"
Write-AuditLog
Write-Host "アンインストール完了。監査ログ: $auditFilePath"
Write-Host "[重要] node_modules / dist / ソースは削除していません。完全削除する場合は手動でインストールルートを削除してください。"
