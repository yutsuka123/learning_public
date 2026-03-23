<#
.SYNOPSIS
ProductionTool アンインストーラ補助の安全消去スモーク試験を実行する。

.DESCRIPTION
[7090][003-0016][009-1021][重要] 実インストール先や実 `ProgramData` を触らず、一時ディレクトリ上で
`uninstall-production-tool.ps1` の上書き消去・監査ログ出力・ディレクトリ削除を検証します。
[厳守] 本試験は擬似データのみを使用し、実運用の鍵素材や監査ログを扱いません。

.EXAMPLE
.\test7090ProductionToolUninstallSmoke.ps1
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRootPath = Resolve-Path (Join-Path $scriptDir "..")
$uninstallScriptPath = Join-Path $projectRootPath "scripts\uninstall-production-tool.ps1"

function ensureDirectory {
  param([string]$directoryPath)
  if (-not (Test-Path $directoryPath)) {
    New-Item -ItemType Directory -Path $directoryPath -Force | Out-Null
  }
}

function writeDummyFile {
  param(
    [string]$filePath,
    [string]$text
  )

  ensureDirectory -directoryPath (Split-Path -Parent $filePath)
  Set-Content -Path $filePath -Value $text -Encoding UTF8
}

$temporaryRootPath = Join-Path $env:TEMP ("ProductionTool-7090-Smoke-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
$fakeInstallRootPath = Join-Path $temporaryRootPath "install-root"
$fakeProgramDataRootPath = Join-Path $temporaryRootPath "programdata-root"
$fakeAuditRootPath = Join-Path $temporaryRootPath "installer-audit"

ensureDirectory -directoryPath $fakeInstallRootPath
ensureDirectory -directoryPath $fakeProgramDataRootPath
ensureDirectory -directoryPath $fakeAuditRootPath

# 擬似インストール先
writeDummyFile -filePath (Join-Path $fakeInstallRootPath "config\productionTool.settings.json") -text '{ "applicationName": "ProductionTool" }'
writeDummyFile -filePath (Join-Path $fakeInstallRootPath "logs\audit-001.json") -text 'dummy audit'
writeDummyFile -filePath (Join-Path $fakeInstallRootPath "scripts\placeholder.txt") -text 'scripts retained'

# 擬似 ProgramData
writeDummyFile -filePath (Join-Path $fakeProgramDataRootPath "keys\key-material.bin") -text 'secret-key-material'
writeDummyFile -filePath (Join-Path $fakeProgramDataRootPath "work\temp-work.bin") -text 'temporary-work'
writeDummyFile -filePath (Join-Path $fakeProgramDataRootPath "logs\audit\run-001.json") -text 'runtime audit'

Write-Host "[setup] temporaryRoot=$temporaryRootPath"
Write-Host "[setup] fakeInstallRoot=$fakeInstallRootPath"
Write-Host "[setup] fakeProgramDataRoot=$fakeProgramDataRootPath"
Write-Host "[setup] fakeAuditRoot=$fakeAuditRootPath"

powershell -ExecutionPolicy Bypass -File $uninstallScriptPath `
  -InstallRoot $fakeInstallRootPath `
  -ProgramDataRoot $fakeProgramDataRootPath `
  -AuditRoot $fakeAuditRootPath `
  -NoConfirm

$expectedRemovedPathList = @(
  (Join-Path $fakeProgramDataRootPath "keys"),
  (Join-Path $fakeProgramDataRootPath "work"),
  (Join-Path $fakeProgramDataRootPath "logs\audit"),
  (Join-Path $fakeInstallRootPath "config"),
  (Join-Path $fakeInstallRootPath "logs")
)

foreach ($expectedRemovedPath in $expectedRemovedPathList) {
  if (Test-Path $expectedRemovedPath) {
    throw "test7090ProductionToolUninstallSmoke failed. removed path still exists. path=$expectedRemovedPath"
  }
}

$retainedScriptsPath = Join-Path $fakeInstallRootPath "scripts"
if (-not (Test-Path $retainedScriptsPath)) {
  throw "test7090ProductionToolUninstallSmoke failed. scripts directory should remain during current session. path=$retainedScriptsPath"
}

$auditFileList = @(Get-ChildItem -Path $fakeAuditRootPath -File -Filter "uninstall-*.json" -ErrorAction SilentlyContinue)
if ($auditFileList.Count -lt 1) {
  throw "test7090ProductionToolUninstallSmoke failed. uninstall audit log is not generated. auditRoot=$fakeAuditRootPath"
}

$latestAuditFile = $auditFileList | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$auditJson = Get-Content -Path $latestAuditFile.FullName -Raw | ConvertFrom-Json

Write-Host "[result] auditFile=$($latestAuditFile.FullName)"
Write-Host "[result] entryCount=$($auditJson.entries.Count)"
Write-Host "[result] smoke test OK"
Write-Host "[note] remove temporaryRoot after inspection: $temporaryRootPath"
