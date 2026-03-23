<#
.SYNOPSIS
ProductionTool をインストールする。

.DESCRIPTION
[003-0016][009-1021][厳守] `production_tool.exe` を `ProductionTool.exe` として配置し、
設定・補助スクリプト・監査ログ/作業領域を作成します。
[重要] `LocalServer` の既存導入痕跡がある場合は混在インストールを禁止します。
[禁止] 機密情報を本スクリプトへ直書きしません。

.PARAMETER InstallRoot
ProductionTool のインストール先です。未指定時は `C:\Program Files\IoT\ProductionTool` を使用します。

.PARAMETER ProgramDataRoot
ProductionTool の ProgramData ルートです。未指定時は `C:\ProgramData\IoT\ProductionTool` を使用します。

.PARAMETER AuditRoot
インストール監査ログの保存先です。未指定時は `C:\ProgramData\IoT\InstallerAudit\ProductionTool` を使用します。

.PARAMETER SkipBuild
`cargo build --release` をスキップします。既に `target\release\production_tool.exe` が存在する場合のみ指定します。

.EXAMPLE
.\install-production-tool.ps1

.EXAMPLE
.\install-production-tool.ps1 -SkipBuild

.EXAMPLE
.\install-production-tool.ps1 -InstallRoot "C:\Temp\pt-install" -ProgramDataRoot "C:\Temp\pt-programdata" -AuditRoot "C:\Temp\pt-audit" -SkipBuild
#>

param(
  [string]$InstallRoot = "C:\Program Files\IoT\ProductionTool",
  [string]$ProgramDataRoot = "C:\ProgramData\IoT\ProductionTool",
  [string]$AuditRoot = "C:\ProgramData\IoT\InstallerAudit\ProductionTool",
  [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRootPath = Resolve-Path (Join-Path $scriptDir "..")
$releaseBinaryPath = Join-Path $projectRootPath "target\release\production_tool.exe"
$auditFilePath = Join-Path $AuditRoot ("install-{0}.json" -f (Get-Date -Format "yyyyMMdd-HHmmss"))

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

function ensureDirectory {
  param([string]$directoryPath)

  if (-not (Test-Path $directoryPath)) {
    New-Item -ItemType Directory -Path $directoryPath -Force | Out-Null
    addAuditEntry -pathText $directoryPath -kindText "create-dir" -resultText "ok"
  } else {
    addAuditEntry -pathText $directoryPath -kindText "create-dir" -resultText "skip" -detailText "already exists"
  }
}

function copyFileChecked {
  param(
    [string]$sourceFilePath,
    [string]$destinationFilePath,
    [switch]$OnlyIfMissing
  )

  if (-not (Test-Path $sourceFilePath)) {
    throw "copyFileChecked failed. source is not found. source=$sourceFilePath"
  }

  ensureDirectory -directoryPath (Split-Path -Parent $destinationFilePath)

  if ($OnlyIfMissing -and (Test-Path $destinationFilePath)) {
    addAuditEntry -pathText $destinationFilePath -kindText "copy-file" -resultText "skip" -detailText "already exists"
    return
  }

  Copy-Item -Path $sourceFilePath -Destination $destinationFilePath -Force
  addAuditEntry -pathText $destinationFilePath -kindText "copy-file" -resultText "ok" -detailText "source=$sourceFilePath"
}

function testLocalServerInstallTraceExists {
  $pathTraceExists = Test-Path "C:\Program Files\IoT\LocalServer"
  $programDataTraceExists = Test-Path "C:\ProgramData\IoT\LocalServer"
  $taskExists = $false
  try {
    $null = Get-ScheduledTask -TaskName "IoT_LocalServer_AutoStart" -ErrorAction Stop
    $taskExists = $true
  } catch {
    $taskExists = $false
  }

  return ($pathTraceExists -or $programDataTraceExists -or $taskExists)
}

function writeAuditLog {
  ensureDirectory -directoryPath $AuditRoot

  $reportObject = [PSCustomObject]@{
    installedAt = (Get-Date).ToString("o")
    installRoot = $InstallRoot
    programDataRoot = $ProgramDataRoot
    auditRoot = $AuditRoot
    executedBy = $env:USERNAME
    entries = $script:auditEntries
  }

  $reportObject | ConvertTo-Json -Depth 6 | Set-Content -Path $auditFilePath -Encoding UTF8
  Write-Host "[audit] log saved: $auditFilePath"
}

function invokeProcessChecked {
  param(
    [Parameter(Mandatory = $true)]
    [string]$filePath,
    [Parameter(Mandatory = $true)]
    [string[]]$argumentList,
    [Parameter(Mandatory = $true)]
    [string]$workingDirectoryPath
  )

  Write-Host "[exec] file=$filePath"
  Write-Host "[exec] args=$($argumentList -join ' ')"
  Write-Host "[exec] cwd=$workingDirectoryPath"

  $process = Start-Process `
    -FilePath $filePath `
    -ArgumentList $argumentList `
    -WorkingDirectory $workingDirectoryPath `
    -NoNewWindow `
    -Wait `
    -PassThru

  if ($process.ExitCode -ne 0) {
    throw "invokeProcessChecked failed. filePath=$filePath exitCode=$($process.ExitCode)"
  }
}

Write-Host "[install] projectRoot=$projectRootPath"
Write-Host "[install] installRoot=$InstallRoot"
Write-Host "[install] programDataRoot=$ProgramDataRoot"
Write-Host "[install] auditRoot=$AuditRoot"

if (testLocalServerInstallTraceExists) {
  addAuditEntry -pathText "mixed-install-check" -kindText "precheck" -resultText "fail" -detailText "LocalServer traces found"
  writeAuditLog
  throw "install-production-tool failed. LocalServer install traces are found. use dedicated factory terminal."
}

addAuditEntry -pathText "install" -kindText "start" -resultText "ok"

if (-not $SkipBuild) {
  Write-Host "[install] running cargo build --release"
  invokeProcessChecked -filePath "cargo" -argumentList @("build", "--release") -workingDirectoryPath $projectRootPath
  addAuditEntry -pathText $releaseBinaryPath -kindText "build-output" -resultText "ok" -detailText "cargo build --release completed"
}

if (-not (Test-Path $releaseBinaryPath)) {
  addAuditEntry -pathText $releaseBinaryPath -kindText "build-output" -resultText "fail" -detailText "not found"
  writeAuditLog
  throw "install-production-tool failed. release binary is not found. path=$releaseBinaryPath"
}

ensureDirectory -directoryPath $InstallRoot
ensureDirectory -directoryPath $ProgramDataRoot
ensureDirectory -directoryPath (Join-Path $ProgramDataRoot "logs")
ensureDirectory -directoryPath (Join-Path $ProgramDataRoot "logs\audit")
ensureDirectory -directoryPath (Join-Path $ProgramDataRoot "work")
ensureDirectory -directoryPath (Join-Path $ProgramDataRoot "keys")

copyFileChecked -sourceFilePath $releaseBinaryPath -destinationFilePath (Join-Path $InstallRoot "ProductionTool.exe")
copyFileChecked -sourceFilePath (Join-Path $projectRootPath "config\productionTool.settings.example.json") -destinationFilePath (Join-Path $InstallRoot "config\productionTool.settings.example.json")
copyFileChecked -sourceFilePath (Join-Path $projectRootPath "config\productionTool.settings.installed.example.json") -destinationFilePath (Join-Path $InstallRoot "config\productionTool.settings.json") -OnlyIfMissing
copyFileChecked -sourceFilePath (Join-Path $projectRootPath "README.md") -destinationFilePath (Join-Path $InstallRoot "docs\README.md")
copyFileChecked -sourceFilePath (Join-Path $projectRootPath "scripts\install-production-tool.ps1") -destinationFilePath (Join-Path $InstallRoot "scripts\install-production-tool.ps1")
copyFileChecked -sourceFilePath (Join-Path $projectRootPath "scripts\uninstall-production-tool.ps1") -destinationFilePath (Join-Path $InstallRoot "scripts\uninstall-production-tool.ps1")
copyFileChecked -sourceFilePath (Join-Path $projectRootPath "scripts\build-production-tool-installer.ps1") -destinationFilePath (Join-Path $InstallRoot "scripts\build-production-tool-installer.ps1")

addAuditEntry -pathText "install" -kindText "finish" -resultText "ok"
writeAuditLog

Write-Host "ProductionTool install helper completed."
Write-Host "Run binary: $InstallRoot\ProductionTool.exe"
