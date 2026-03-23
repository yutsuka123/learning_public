<#
.SYNOPSIS
ProductionTool インストール補助の擬似環境スモーク試験を実行する。

.DESCRIPTION
[003-0016][009-1021][重要] `install-production-tool.ps1` を一時ディレクトリ上で実行し、
`ProductionTool.exe`、設定、補助スクリプト、ProgramData 領域、監査ログが生成されることを確認します。
[厳守] 実インストール先や実 ProgramData は変更しません。

.EXAMPLE
.\testProductionToolInstallHelperSmoke.ps1
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRootPath = Resolve-Path (Join-Path $scriptDir "..")
$installScriptPath = Join-Path $projectRootPath "scripts\install-production-tool.ps1"

$temporaryRootPath = Join-Path $env:TEMP ("ProductionTool-Install-Smoke-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
$fakeInstallRootPath = Join-Path $temporaryRootPath "install-root"
$fakeProgramDataRootPath = Join-Path $temporaryRootPath "programdata-root"
$fakeAuditRootPath = Join-Path $temporaryRootPath "installer-audit"

Write-Host "[setup] temporaryRoot=$temporaryRootPath"
Write-Host "[setup] fakeInstallRoot=$fakeInstallRootPath"
Write-Host "[setup] fakeProgramDataRoot=$fakeProgramDataRootPath"
Write-Host "[setup] fakeAuditRoot=$fakeAuditRootPath"

powershell -ExecutionPolicy Bypass -File $installScriptPath `
  -InstallRoot $fakeInstallRootPath `
  -ProgramDataRoot $fakeProgramDataRootPath `
  -AuditRoot $fakeAuditRootPath `
  -SkipBuild

$expectedFilePathList = @(
  (Join-Path $fakeInstallRootPath "ProductionTool.exe"),
  (Join-Path $fakeInstallRootPath "config\productionTool.settings.example.json"),
  (Join-Path $fakeInstallRootPath "config\productionTool.settings.json"),
  (Join-Path $fakeInstallRootPath "scripts\install-production-tool.ps1"),
  (Join-Path $fakeInstallRootPath "scripts\uninstall-production-tool.ps1"),
  (Join-Path $fakeInstallRootPath "scripts\build-production-tool-installer.ps1"),
  (Join-Path $fakeInstallRootPath "docs\README.md")
)

foreach ($expectedFilePath in $expectedFilePathList) {
  if (-not (Test-Path $expectedFilePath)) {
    throw "testProductionToolInstallHelperSmoke failed. expected file is not found. path=$expectedFilePath"
  }
}

$expectedDirectoryPathList = @(
  (Join-Path $fakeProgramDataRootPath "logs\audit"),
  (Join-Path $fakeProgramDataRootPath "work"),
  (Join-Path $fakeProgramDataRootPath "keys")
)

foreach ($expectedDirectoryPath in $expectedDirectoryPathList) {
  if (-not (Test-Path $expectedDirectoryPath)) {
    throw "testProductionToolInstallHelperSmoke failed. expected directory is not found. path=$expectedDirectoryPath"
  }
}

$auditFileList = @(Get-ChildItem -Path $fakeAuditRootPath -File -Filter "install-*.json" -ErrorAction SilentlyContinue)
if ($auditFileList.Count -lt 1) {
  throw "testProductionToolInstallHelperSmoke failed. install audit log is not generated. auditRoot=$fakeAuditRootPath"
}

$latestAuditFile = $auditFileList | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$auditJson = Get-Content -Path $latestAuditFile.FullName -Raw | ConvertFrom-Json

Write-Host "[result] auditFile=$($latestAuditFile.FullName)"
Write-Host "[result] entryCount=$($auditJson.entries.Count)"
Write-Host "[result] install helper smoke test OK"
Write-Host "[note] remove temporaryRoot after inspection: $temporaryRootPath"
