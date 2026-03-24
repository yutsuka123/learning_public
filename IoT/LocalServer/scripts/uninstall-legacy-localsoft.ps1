<#
.SYNOPSIS
廃止対象の LocalSoft / LocalSoftForCloud 資産を安全消去する。

.DESCRIPTION
[003-0017][重要] LocalSoft 廃止時の移行タスクとして、旧保存 DB / キャッシュ / 機密設定を上書き消去してから削除する。
[厳守] 通常削除のみで機密残留を許容しない。
[厳守] 消去結果は JSON 監査ログへ保存する。

.PARAMETER LegacyRoots
消去対象の旧ルートパス配列。未指定時は代表的な既定候補を使用する。

.PARAMETER AuditRoot
監査ログの保存先ルート。

.PARAMETER NoConfirm
確認プロンプトを省略する。

.EXAMPLE
powershell -ExecutionPolicy Bypass -File ".\scripts\uninstall-legacy-localsoft.ps1" -NoConfirm

.EXAMPLE
powershell -ExecutionPolicy Bypass -File ".\scripts\uninstall-legacy-localsoft.ps1" -LegacyRoots @("C:\work\old\LocalSoft","C:\work\old\LocalSoftForCloud") -NoConfirm
#>

param(
  [string[]]$LegacyRoots = @(),
  [string]$AuditRoot = "",
  [switch]$NoConfirm
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($LegacyRoots.Count -eq 0) {
  $LegacyRoots = @(
    "C:\Program Files\IoT\LocalSoft",
    "C:\Program Files\IoT\LocalSoftForCloud",
    "C:\ProgramData\IoT\LocalSoft",
    "C:\ProgramData\IoT\LocalSoftForCloud"
  )
}

if ([string]::IsNullOrWhiteSpace($AuditRoot)) {
  $AuditRoot = Join-Path $env:LOCALAPPDATA "IoT-LegacyLocalSoft-Uninstall"
}

$timestampText = (Get-Date).ToString("yyyyMMdd-HHmmss")
$auditFilePath = Join-Path $AuditRoot "audit-$timestampText.json"
$script:auditEntries = @()

function Add-AuditEntry {
  param(
    [string]$Path,
    [string]$Kind,
    [string]$Result,
    [string]$Detail = ""
  )
  $script:auditEntries += [PSCustomObject]@{
    path = $Path
    kind = $Kind
    result = $Result
    detail = $Detail
    at = (Get-Date).ToString("o")
  }
}

function Write-AuditLog {
  if (-not (Test-Path $AuditRoot)) {
    New-Item -ItemType Directory -Path $AuditRoot -Force | Out-Null
  }
  $report = @{
    testId = "003-0017"
    executedAt = (Get-Date).ToString("o")
    executedBy = $env:USERNAME
    legacyRoots = $LegacyRoots
    entries = $script:auditEntries
  }
  $report | ConvertTo-Json -Depth 6 | Set-Content -Path $auditFilePath -Encoding UTF8
  Write-Host "[audit] log saved: $auditFilePath"
}

function Secure-DeleteFile {
  param([string]$FilePath)

  if (-not (Test-Path $FilePath)) {
    Add-AuditEntry -Path $FilePath -Kind "secure-delete" -Result "skip" -Detail "not found"
    return
  }

  try {
    $fileInfo = Get-Item $FilePath -Force
    if ($fileInfo.PSIsContainer) {
      Add-AuditEntry -Path $FilePath -Kind "secure-delete" -Result "skip" -Detail "is directory"
      return
    }

    $fileLength = [int64]$fileInfo.Length
    if ($fileLength -gt 0) {
      $randomBytes = New-Object byte[] $fileLength
      $randomNumberGenerator = [System.Security.Cryptography.RandomNumberGenerator]::Create()
      try {
        $randomNumberGenerator.GetBytes($randomBytes)
      } finally {
        $randomNumberGenerator.Dispose()
      }
      [System.IO.File]::WriteAllBytes($FilePath, $randomBytes)
    }
    Remove-Item $FilePath -Force
    Add-AuditEntry -Path $FilePath -Kind "secure-delete" -Result "ok" -Detail "overwrite+delete bytes=$fileLength"
  } catch {
    Add-AuditEntry -Path $FilePath -Kind "secure-delete" -Result "fail" -Detail $_.Exception.Message
    throw
  }
}

function Remove-LegacyRoot {
  param([string]$RootPath)

  if (-not (Test-Path $RootPath)) {
    Add-AuditEntry -Path $RootPath -Kind "legacy-root" -Result "skip" -Detail "not found"
    return
  }

  Add-AuditEntry -Path $RootPath -Kind "legacy-root" -Result "start"

  $targetFilePatterns = @("*.db", "*.sqlite", "*.sqlite3", "*.json", "*.bin", "*.pem", "*.key", "*.log", "*.cache")
  foreach ($pattern in $targetFilePatterns) {
    $fileList = @(Get-ChildItem $RootPath -File -Recurse -Filter $pattern -ErrorAction SilentlyContinue)
    foreach ($file in $fileList) {
      Secure-DeleteFile -FilePath $file.FullName
    }
  }

  $remainingFileList = @(Get-ChildItem $RootPath -File -Recurse -ErrorAction SilentlyContinue)
  foreach ($remainingFile in $remainingFileList) {
    Secure-DeleteFile -FilePath $remainingFile.FullName
  }

  try {
    Remove-Item $RootPath -Recurse -Force -ErrorAction Stop
    Add-AuditEntry -Path $RootPath -Kind "legacy-root" -Result "ok" -Detail "removed"
  } catch {
    Add-AuditEntry -Path $RootPath -Kind "legacy-root" -Result "fail" -Detail $_.Exception.Message
    throw
  }
}

if (-not $NoConfirm) {
  $confirmMessage = "legacy LocalSoft assets will be secure-deleted. roots=`"$($LegacyRoots -join ', ')`""
  $confirmResult = Read-Host "$confirmMessage`nContinue? (y/N)"
  if ($confirmResult -notmatch "^[yY]") {
    exit 0
  }
}

Add-AuditEntry -Path "uninstall-legacy-localsoft" -Kind "start" -Result "ok" -Detail "roots=$($LegacyRoots -join ';')"

foreach ($legacyRoot in $LegacyRoots) {
  Remove-LegacyRoot -RootPath $legacyRoot
}

Add-AuditEntry -Path "uninstall-legacy-localsoft" -Kind "finish" -Result "ok"
Write-AuditLog
Write-Host "legacy LocalSoft uninstall completed. audit log: $auditFilePath"
