<#
.SYNOPSIS
ProductionTool をアンインストールし、機密データを安全消去する。

.DESCRIPTION
[003-0016][009-1021][厳守] `安全消去ポリシー.md` 4.2 に従い、鍵素材・実行履歴・一時ファイルを上書き消去してから削除します。
[重要] 消去成否は `C:\ProgramData\IoT\InstallerAudit\ProductionTool\` へ JSON 監査ログとして保存します。
[禁止] 通常削除のみで機密残留を許容しません。

.PARAMETER InstallRoot
ProductionTool のインストールルートです。未指定時はスクリプト配置先の親ディレクトリを使用します。

.PARAMETER AuditRoot
アンインストール監査ログの保存先です。未指定時は `C:\ProgramData\IoT\InstallerAudit\ProductionTool\` を使用します。

.PARAMETER ProgramDataRoot
ProductionTool の `ProgramData` ルートです。未指定時は `C:\ProgramData\IoT\ProductionTool\` を使用します。試験時のみ差し替えます。

.PARAMETER NoConfirm
確認プロンプトをスキップします。インストーラ連携時は指定必須です。

.EXAMPLE
.\uninstall-production-tool.ps1

.EXAMPLE
.\uninstall-production-tool.ps1 -InstallRoot "C:\Program Files\IoT\ProductionTool" -NoConfirm

.EXAMPLE
.\uninstall-production-tool.ps1 -InstallRoot "C:\Temp\pt-install" -ProgramDataRoot "C:\Temp\pt-programdata" -AuditRoot "C:\Temp\pt-audit" -NoConfirm
#>

param(
  [string]$InstallRoot = "",
  [string]$AuditRoot = "",
  [string]$ProgramDataRoot = "",
  [switch]$NoConfirm
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$defaultInstallRoot = Resolve-Path (Join-Path $scriptDir "..")
$resolvedInstallRoot = if ($InstallRoot) { Resolve-Path $InstallRoot } else { $defaultInstallRoot }
$resolvedAuditRoot = if ($AuditRoot) { $AuditRoot } else { "C:\ProgramData\IoT\InstallerAudit\ProductionTool" }
$resolvedProgramDataRoot = if ($ProgramDataRoot) { $ProgramDataRoot } else { "C:\ProgramData\IoT\ProductionTool" }
$timestampText = Get-Date -Format "yyyyMMdd-HHmmss"
$auditFilePath = Join-Path $resolvedAuditRoot "uninstall-$timestampText.json"
$keysRootPath = Join-Path $resolvedProgramDataRoot "keys"
$workRootPath = Join-Path $resolvedProgramDataRoot "work"
$auditLogsRootPath = Join-Path $resolvedProgramDataRoot "logs\audit"

$script:auditEntries = @()

function addAuditEntry {
  param(
    [string]$pathText,
    [string]$kindText,
    [string]$resultText,
    [string]$detailText = ""
  )

  $script:auditEntries += [PSCustomObject]@{
    path = $pathText
    kind = $kindText
    result = $resultText
    detail = $detailText
    at = (Get-Date).ToString("o")
  }
}

function writeAuditLog {
  if (-not (Test-Path $resolvedAuditRoot)) {
    New-Item -ItemType Directory -Path $resolvedAuditRoot -Force | Out-Null
  }

  $reportObject = [PSCustomObject]@{
    uninstalledAt = (Get-Date).ToString("o")
    installRoot = $resolvedInstallRoot
    auditRoot = $resolvedAuditRoot
    programDataRoot = $resolvedProgramDataRoot
    executedBy = $env:USERNAME
    entries = $script:auditEntries
  }

  $reportObject | ConvertTo-Json -Depth 6 | Set-Content -Path $auditFilePath -Encoding UTF8
  Write-Host "[audit] log saved: $auditFilePath"
}

function secureDeleteFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$filePath
  )

  if (-not (Test-Path $filePath)) {
    addAuditEntry -pathText $filePath -kindText "secure-delete" -resultText "skip" -detailText "not found"
    return
  }

  try {
    $fileItem = Get-Item $filePath -Force
    if ($fileItem.PSIsContainer) {
      addAuditEntry -pathText $filePath -kindText "secure-delete" -resultText "skip" -detailText "directory"
      return
    }

    $fileLength = [int64]$fileItem.Length
    $overwriteBytes = New-Object byte[] $fileLength
    $randomNumberGenerator = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    try {
      $randomNumberGenerator.GetBytes($overwriteBytes)
    } finally {
      $randomNumberGenerator.Dispose()
    }
    [System.IO.File]::WriteAllBytes($filePath, $overwriteBytes)
    Remove-Item $filePath -Force
    addAuditEntry -pathText $filePath -kindText "secure-delete" -resultText "ok" -detailText "overwrite+delete bytes=$fileLength"
  } catch {
    addAuditEntry -pathText $filePath -kindText "secure-delete" -resultText "fail" -detailText $_.Exception.Message
    throw
  }
}

function secureDeleteDirectoryFiles {
  param(
    [Parameter(Mandatory = $true)]
    [string]$directoryPath
  )

  if (-not (Test-Path $directoryPath)) {
    addAuditEntry -pathText $directoryPath -kindText "secure-delete-directory" -resultText "skip" -detailText "not found"
    return
  }

  Get-ChildItem -Path $directoryPath -File -Recurse -ErrorAction SilentlyContinue | ForEach-Object {
    $currentFilePath = $_.FullName
    try {
      secureDeleteFile -filePath $currentFilePath
    } catch {
      Write-Warning "secureDeleteDirectoryFiles failed. path=$currentFilePath detail=$($_.Exception.Message)"
    }
  }
}

function removeDirectoryTree {
  param(
    [Parameter(Mandatory = $true)]
    [string]$directoryPath
  )

  if (-not (Test-Path $directoryPath)) {
    addAuditEntry -pathText $directoryPath -kindText "remove-dir" -resultText "skip" -detailText "not found"
    return
  }

  try {
    Remove-Item $directoryPath -Recurse -Force -ErrorAction Stop
    addAuditEntry -pathText $directoryPath -kindText "remove-dir" -resultText "ok"
  } catch {
    addAuditEntry -pathText $directoryPath -kindText "remove-dir" -resultText "fail" -detailText $_.Exception.Message
    throw
  }
}

function stopProductionToolProcess {
  $candidateProcesses = @("ProductionTool", "production_tool")
  foreach ($processName in $candidateProcesses) {
    Get-Process -Name $processName -ErrorAction SilentlyContinue | ForEach-Object {
      try {
        $_ | Stop-Process -Force -ErrorAction Stop
        addAuditEntry -pathText "process" -kindText "stop" -resultText "ok" -detailText "name=$processName pid=$($_.Id)"
      } catch {
        addAuditEntry -pathText "process" -kindText "stop" -resultText "fail" -detailText "name=$processName pid=$($_.Id) $($_.Exception.Message)"
      }
    }
  }
}

if (-not $NoConfirm) {
  $messageText = "ProductionTool uninstall will overwrite-delete keys, work files, and audit logs. installRoot=$resolvedInstallRoot"
  $answerText = Read-Host "$messageText`nContinue? (y/N)"
  if ($answerText -notmatch '^[yY]') {
    exit 0
  }
}

addAuditEntry -pathText "uninstall" -kindText "start" -resultText "ok" -detailText "installRoot=$resolvedInstallRoot"

# 1. 実行中プロセス停止
stopProductionToolProcess
Start-Sleep -Seconds 2

# 2. 鍵素材・一時領域・監査ログを上書き消去
secureDeleteDirectoryFiles -directoryPath $keysRootPath
secureDeleteDirectoryFiles -directoryPath $workRootPath
secureDeleteDirectoryFiles -directoryPath $auditLogsRootPath

# 3. インストール先配下の設定・ログも上書き消去
$installConfigDirectoryPath = Join-Path $resolvedInstallRoot "config"
$installLogsDirectoryPath = Join-Path $resolvedInstallRoot "logs"
$installScriptsDirectoryPath = Join-Path $resolvedInstallRoot "scripts"

secureDeleteDirectoryFiles -directoryPath $installConfigDirectoryPath
secureDeleteDirectoryFiles -directoryPath $installLogsDirectoryPath

# 4. ディレクトリ削除
removeDirectoryTree -directoryPath $keysRootPath
removeDirectoryTree -directoryPath $workRootPath
removeDirectoryTree -directoryPath $auditLogsRootPath
removeDirectoryTree -directoryPath $installConfigDirectoryPath
removeDirectoryTree -directoryPath $installLogsDirectoryPath

if (Test-Path $installScriptsDirectoryPath) {
  addAuditEntry -pathText $installScriptsDirectoryPath -kindText "retain-dir" -resultText "skip" -detailText "kept for uninstaller execution during current session"
}

addAuditEntry -pathText "uninstall" -kindText "finish" -resultText "ok"
writeAuditLog

Write-Host "ProductionTool secure uninstall helper completed."
Write-Host "Remaining installer-managed files will be removed by Inno Setup after this helper exits."
