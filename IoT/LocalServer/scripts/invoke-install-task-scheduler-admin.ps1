<#
.SYNOPSIS
install-task-scheduler.ps1 を管理者権限の新しい PowerShell で起動する。

.DESCRIPTION
[重要] Task Scheduler の登録・削除は管理者権限が必要なため、通常の Cursor/ターミナルから直叩きすると 0x80070005 になることがある。
[厳守] UAC 承認ダイアログが表示されたら許可する。
[禁止] このスクリプトへ機密情報を直書きしない。

.PARAMETER TriggerMode
install-task-scheduler.ps1 に渡す -TriggerMode（既定: AtLogOn）。
#>

param(
  [ValidateSet("AtLogOn", "AtStartup")]
  [string]$TriggerMode = "AtLogOn"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$installerPath = Join-Path $scriptDir "install-task-scheduler.ps1"

if (-not (Test-Path $installerPath)) {
  throw "invoke-install-task-scheduler-admin.ps1 failed. installer not found. installerPath=$installerPath"
}

$argumentList = @(
  "-NoProfile",
  "-ExecutionPolicy", "Bypass",
  "-File", $installerPath,
  "-TriggerMode", $TriggerMode
)

Write-Host "[invoke-install-task-scheduler-admin] UAC を求められたら許可してください。TriggerMode=$TriggerMode"
$process = Start-Process -FilePath "powershell.exe" -Verb RunAs -ArgumentList $argumentList -PassThru -Wait
if ($process.ExitCode -ne 0) {
  throw "invoke-install-task-scheduler-admin.ps1 failed. elevated powershell exitCode=$($process.ExitCode) TriggerMode=$TriggerMode"
}
Write-Host "[invoke-install-task-scheduler-admin] completed. exitCode=0"
