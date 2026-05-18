<#
.SYNOPSIS
LocalServer自動起動タスクを作成・更新する。

.DESCRIPTION
[重要] Windows Task Schedulerに「PC起動時」または「ユーザー・ログオン時」トリガーのタスクを登録する。
[厳守] 実行ユーザーに Node 実行権限があること。
[禁止] 既存の他用途タスク名を流用しない。

[仕様変更][2026-05-17] 既定トリガーを AtLogOn + Interactive に変更した。
理由: AtStartup + S4U の組み合わせでは `LastTaskResult=267011`（0x41303 = SCHED_S_TASK_HAS_NOT_RUN）
相当でタスク本体が起動しない事象が確認されたため。ログオン後の対話セッション上で起動する方式を正とする。
起動直後（ログオン前）に必須とする場合は `-TriggerMode AtStartup` を明示し、別途実行主体の見直しを検討する。
#>

param(
  [Parameter(Mandatory = $false)]
  [ValidateSet("AtLogOn", "AtStartup")]
  [string]$TriggerMode = "AtLogOn"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function testIsRunningAsAdmin {
  <#
  .SYNOPSIS
  現在の PowerShell が管理者として実行中か判定する。

  .DESCRIPTION
  [重要] Register-ScheduledTask / Unregister-ScheduledTask は管理者権限が無いと 0x80070005 になり得る。
  #>
  $windowsIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $windowsPrincipal = New-Object Security.Principal.WindowsPrincipal($windowsIdentity)
  return $windowsPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (testIsRunningAsAdmin)) {
  $scriptPath = $MyInvocation.MyCommand.Path
  $helpMessage = @"
install-task-scheduler.ps1: 管理者権限が必要です（Task Scheduler 登録のため）。

対処:
  1) PowerShell を「管理者として実行」し、次を実行する:
     Set-Location '$(Split-Path -Parent $MyInvocation.MyCommand.Path)'
     .\install-task-scheduler.ps1
  2) または（UAC 昇格）:
     .\invoke-install-task-scheduler-admin.ps1

現在のスクリプト: $scriptPath
パラメータ例: -TriggerMode AtLogOn（既定） / -TriggerMode AtStartup
"@
  throw $helpMessage
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$runnerPath = Resolve-Path (Join-Path $scriptDir "run-local-server.ps1")
$taskName = "IoT_LocalServer_AutoStart"
$userId = "{0}\{1}" -f $env:USERDOMAIN, $env:USERNAME

$action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$runnerPath`""

if ($TriggerMode -eq "AtStartup") {
  $trigger = New-ScheduledTaskTrigger -AtStartup
  <#
    AtStartup はログオン前に発火する。ユーザー主体の場合 S4U が選ばれがちだが、環境によっては
    セッションが無くタスクが実行されないことがある（試験記録 7001 / 問題点 #0042 参照）。
  #>
  $principal = New-ScheduledTaskPrincipal -UserId $userId -LogonType S4U -RunLevel Limited
} else {
  $trigger = New-ScheduledTaskTrigger -AtLogOn -User $userId
  <#
    ログオン時は対話セッション上で実行し、ユーザー環境・プロファイル前提の node 解決を安定させる。
  #>
  $principal = New-ScheduledTaskPrincipal -UserId $userId -LogonType Interactive -RunLevel Limited
}

$settings = New-ScheduledTaskSettingsSet `
  -StartWhenAvailable `
  -ExecutionTimeLimit (New-TimeSpan -Hours 0) `
  -RestartCount 999 `
  -RestartInterval (New-TimeSpan -Minutes 1) `
  -MultipleInstances IgnoreNew

if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
  Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
}

Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings | Out-Null
Write-Host "Task registered: $taskName userId=$userId TriggerMode=$TriggerMode"
