<#
.SYNOPSIS
009-1022: 工場専用端末（LocalServer 非導入）最終再検証の証跡を採取する。

.DESCRIPTION
[重要] 本スクリプトは `ProductionToolSetup.exe` の install -> 起動 -> uninstall を手動実施した後、
結果確認を定型化して JSON レポートへ保存する。
[厳守] 判定は「工場専用端末に LocalServer が混在していないこと」を含む。
[禁止] 本スクリプトはインストーラ/アンインストーラの実行を自動化しない（誤操作防止）。

主仕様:
- `preInstall` / `postInstall` / `postUninstall` の各フェーズで判定項目を確認する。
- 判定結果（OK/NG）と詳細メッセージを JSON レポートへ保存する。
- NG がある場合は終了コード 1 を返す。

制限事項:
- `postInstall` / `postUninstall` では、手動実施済みの install/uninstall を前提にする。
- スケジュールタスク確認（`IoT_LocalServer_AutoStart`）は環境差分があるため、取得失敗時は warning 扱いにする。

.PARAMETER phase
検証フェーズ。`preInstall` / `postInstall` / `postUninstall` のいずれか。

.PARAMETER installRoot
ProductionTool のインストール先。

.PARAMETER programDataRoot
ProductionTool の ProgramData ルート。

.PARAMETER installerAuditRoot
InstallerAudit の保存先。

.PARAMETER reportRoot
レポート保存先。

.EXAMPLE
powershell -ExecutionPolicy Bypass -File ".\tests\test0091022FactoryTerminalFinalValidation.ps1" -phase preInstall

.EXAMPLE
powershell -ExecutionPolicy Bypass -File ".\tests\test0091022FactoryTerminalFinalValidation.ps1" -phase postInstall

.EXAMPLE
powershell -ExecutionPolicy Bypass -File ".\tests\test0091022FactoryTerminalFinalValidation.ps1" -phase postUninstall
#>

param(
  [ValidateSet("preInstall", "postInstall", "postUninstall")]
  [string]$phase = "preInstall",
  [string]$installRoot = "C:\Program Files\IoT\ProductionTool",
  [string]$programDataRoot = "C:\ProgramData\IoT\ProductionTool",
  [string]$installerAuditRoot = "C:\ProgramData\IoT\InstallerAudit\ProductionTool",
  [string]$reportRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($reportRoot)) {
  $scriptDirectoryPath = Split-Path -Parent $MyInvocation.MyCommand.Path
  $reportRoot = Join-Path $scriptDirectoryPath "logs"
}

if (-not (Test-Path $reportRoot)) {
  New-Item -ItemType Directory -Path $reportRoot -Force | Out-Null
}

$reportTimestampText = (Get-Date).ToString("yyyyMMdd-HHmmss")
$reportFilePath = Join-Path $reportRoot ("test0091022-{0}-{1}.json" -f $phase, $reportTimestampText)
$script:checkResults = @()

function addCheckResult {
  param(
    [string]$checkName,
    [bool]$ok,
    [string]$detail
  )
  $script:checkResults += [PSCustomObject]@{
    checkName = $checkName
    ok = $ok
    detail = $detail
    checkedAt = (Get-Date).ToString("o")
  }
}

function assertPathExists {
  param(
    [string]$checkName,
    [string]$pathValue
  )
  $exists = Test-Path $pathValue
  addCheckResult -checkName $checkName -ok $exists -detail ("path={0}" -f $pathValue)
}

function assertPathNotExists {
  param(
    [string]$checkName,
    [string]$pathValue
  )
  $exists = Test-Path $pathValue
  addCheckResult -checkName $checkName -ok (-not $exists) -detail ("path={0}" -f $pathValue)
}

function checkLocalServerAbsence {
  $localServerInstallRootPath = "C:\Program Files\IoT\LocalServer"
  $localServerProgramDataPath = "C:\ProgramData\IoT\LocalServer"
  assertPathNotExists -checkName "localServer.installRootAbsent" -pathValue $localServerInstallRootPath
  assertPathNotExists -checkName "localServer.programDataAbsent" -pathValue $localServerProgramDataPath

  try {
    $taskExists = $false
    $taskQueryOutput = & schtasks /query /tn "IoT_LocalServer_AutoStart" 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0 -and $taskQueryOutput -match "IoT_LocalServer_AutoStart") {
      $taskExists = $true
    }
    addCheckResult -checkName "localServer.scheduledTaskAbsent" -ok (-not $taskExists) -detail ("taskName=IoT_LocalServer_AutoStart")
  } catch {
    addCheckResult -checkName "localServer.scheduledTaskAbsent" -ok $true -detail ("warning=function=checkLocalServerAbsence failedToQueryScheduledTask detail={0}" -f $_.Exception.Message)
  }
}

function checkPostInstallState {
  assertPathExists -checkName "productionTool.installRootExists" -pathValue $installRoot
  assertPathExists -checkName "productionTool.exeExists" -pathValue (Join-Path $installRoot "ProductionTool.exe")
  assertPathExists -checkName "productionTool.configExists" -pathValue (Join-Path $installRoot "config\productionTool.settings.json")
  assertPathExists -checkName "productionTool.installScriptExists" -pathValue (Join-Path $installRoot "scripts\install-production-tool.ps1")
  assertPathExists -checkName "productionTool.uninstallScriptExists" -pathValue (Join-Path $installRoot "scripts\uninstall-production-tool.ps1")
  assertPathExists -checkName "productionTool.programDataAuditExists" -pathValue (Join-Path $programDataRoot "logs\audit")
  assertPathExists -checkName "productionTool.programDataWorkExists" -pathValue (Join-Path $programDataRoot "work")
  assertPathExists -checkName "productionTool.programDataKeysExists" -pathValue (Join-Path $programDataRoot "keys")
}

function checkPreInstallState {
  assertPathNotExists -checkName "productionTool.exeAbsentBeforeInstall" -pathValue (Join-Path $installRoot "ProductionTool.exe")
  assertPathNotExists -checkName "productionTool.configAbsentBeforeInstall" -pathValue (Join-Path $installRoot "config\productionTool.settings.json")
  assertPathNotExists -checkName "productionTool.programDataAuditAbsentBeforeInstall" -pathValue (Join-Path $programDataRoot "logs\audit")
  assertPathNotExists -checkName "productionTool.programDataWorkAbsentBeforeInstall" -pathValue (Join-Path $programDataRoot "work")
  assertPathNotExists -checkName "productionTool.programDataKeysAbsentBeforeInstall" -pathValue (Join-Path $programDataRoot "keys")
}

function checkPostUninstallState {
  assertPathNotExists -checkName "productionTool.exeAbsent" -pathValue (Join-Path $installRoot "ProductionTool.exe")
  assertPathNotExists -checkName "productionTool.configAbsent" -pathValue (Join-Path $installRoot "config")
  assertPathNotExists -checkName "productionTool.programDataAuditAbsent" -pathValue (Join-Path $programDataRoot "logs\audit")
  assertPathNotExists -checkName "productionTool.programDataWorkAbsent" -pathValue (Join-Path $programDataRoot "work")
  assertPathNotExists -checkName "productionTool.programDataKeysAbsent" -pathValue (Join-Path $programDataRoot "keys")

  $uninstallAuditFileList = @()
  if (Test-Path $installerAuditRoot) {
    $uninstallAuditFileList = @(Get-ChildItem -Path $installerAuditRoot -File -Filter "uninstall-*.json" -ErrorAction SilentlyContinue)
  }
  $hasUninstallAuditFile = $uninstallAuditFileList.Count -ge 1
  addCheckResult -checkName "productionTool.uninstallAuditExists" -ok $hasUninstallAuditFile -detail ("auditRoot={0} count={1}" -f $installerAuditRoot, $uninstallAuditFileList.Count)
}

checkLocalServerAbsence

switch ($phase) {
  "preInstall" {
    checkPreInstallState
  }
  "postInstall" {
    checkPostInstallState
  }
  "postUninstall" {
    checkPostUninstallState
  }
  default {
    throw ("main failed. unsupported phase. phase={0}" -f $phase)
  }
}

$ngItemList = @($script:checkResults | Where-Object { -not $_.ok })
$resultText = if ($ngItemList.Count -eq 0) { "OK" } else { "NG" }

$reportObject = [PSCustomObject]@{
  testId = "009-1022"
  phase = $phase
  result = $resultText
  checkedAt = (Get-Date).ToString("o")
  machineName = $env:COMPUTERNAME
  executedBy = $env:USERNAME
  installRoot = $installRoot
  programDataRoot = $programDataRoot
  installerAuditRoot = $installerAuditRoot
  checkResults = $script:checkResults
}

$reportObject | ConvertTo-Json -Depth 8 | Set-Content -Path $reportFilePath -Encoding UTF8

Write-Host ("[009-1022] phase={0} result={1}" -f $phase, $resultText)
Write-Host ("[009-1022] reportFilePath={0}" -f $reportFilePath)

if ($ngItemList.Count -gt 0) {
  foreach ($ngItem in $ngItemList) {
    Write-Host ("[009-1022][NG] checkName={0} detail={1}" -f $ngItem.checkName, $ngItem.detail)
  }
  exit 1
}
