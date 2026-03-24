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
  Write-Host "[audit] log saved: $auditFilePath"
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
    $randomNumberGenerator = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    try {
      $randomNumberGenerator.GetBytes($bytes)
    } finally {
      $randomNumberGenerator.Dispose()
    }
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
  $msg = "LocalServer uninstall will overwrite-delete sensitive data. installRoot=$InstallRoot"
  $r = Read-Host "$msg`nContinue? (y/N)"
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
  $logFileList = @(Get-ChildItem $logsDir -File -Filter "*.log" -ErrorAction SilentlyContinue)
  foreach ($logFile in $logFileList) {
    $currentFilePath = $logFile.FullName
    try { Secure-DeleteFile -FilePath $currentFilePath } catch { Write-Warning "Secure-Delete failed: $currentFilePath $($_.Exception.Message)" }
  }
}

$uploadsDir = Join-Path $InstallRoot "uploads"
if (Test-Path $uploadsDir) {
  $uploadFileList = @(Get-ChildItem $uploadsDir -File -Recurse -ErrorAction SilentlyContinue)
  foreach ($uploadFile in $uploadFileList) {
    $currentFilePath = $uploadFile.FullName
    try { Secure-DeleteFile -FilePath $currentFilePath } catch { Write-Warning "Secure-Delete failed: $currentFilePath $($_.Exception.Message)" }
  }
}

$certsDir = Join-Path $InstallRoot "certs"
if (Test-Path $certsDir) {
  $certFileList = @(Get-ChildItem $certsDir -File -ErrorAction SilentlyContinue)
  foreach ($certFile in $certFileList) {
    $currentFilePath = $certFile.FullName
    try { Secure-DeleteFile -FilePath $currentFilePath } catch { Write-Warning "Secure-Delete failed: $currentFilePath $($_.Exception.Message)" }
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
Write-Host "LocalServer uninstall completed. audit log: $auditFilePath"
Write-Host "[important] node_modules, dist, and source files are not removed. Delete the install root manually if you need full removal."
