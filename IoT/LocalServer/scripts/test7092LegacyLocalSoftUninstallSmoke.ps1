<#
.SYNOPSIS
7092: LocalSoft 廃止アンインストール安全消去のスモーク試験を実施する。

.DESCRIPTION
[003-0017][重要] 旧 LocalSoft / LocalSoftForCloud の擬似資産を一時ディレクトリへ作成し、
`uninstall-legacy-localsoft.ps1` による上書き消去 + 削除 + 監査ログ生成を確認する。

.EXAMPLE
powershell -ExecutionPolicy Bypass -File ".\scripts\test7092LegacyLocalSoftUninstallSmoke.ps1"
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDirectoryPath = Split-Path -Parent $MyInvocation.MyCommand.Path
$targetScriptPath = Join-Path $scriptDirectoryPath "uninstall-legacy-localsoft.ps1"
if (-not (Test-Path $targetScriptPath)) {
  throw "test7092 failed. uninstall script is not found. path=$targetScriptPath"
}

$temporaryRootPath = Join-Path ([System.IO.Path]::GetTempPath()) ("test7092-{0}" -f ([System.Guid]::NewGuid().ToString("N")))
$legacyLocalSoftPath = Join-Path $temporaryRootPath "LocalSoft"
$legacyLocalSoftForCloudPath = Join-Path $temporaryRootPath "LocalSoftForCloud"
$auditRootPath = Join-Path $temporaryRootPath "audit"

New-Item -ItemType Directory -Path (Join-Path $legacyLocalSoftPath "data") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $legacyLocalSoftPath "cache") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $legacyLocalSoftForCloudPath "config") -Force | Out-Null

Set-Content -Path (Join-Path $legacyLocalSoftPath "data\device.db") -Value "dummy-db" -Encoding UTF8
Set-Content -Path (Join-Path $legacyLocalSoftPath "cache\cache.json") -Value "{""cache"":true}" -Encoding UTF8
Set-Content -Path (Join-Path $legacyLocalSoftForCloudPath "config\secret.key") -Value "dummy-secret" -Encoding UTF8

Write-Host "[7092] fixture prepared:"
Write-Host "  $legacyLocalSoftPath"
Write-Host "  $legacyLocalSoftForCloudPath"

& $targetScriptPath `
  -LegacyRoots @($legacyLocalSoftPath, $legacyLocalSoftForCloudPath) `
  -AuditRoot $auditRootPath `
  -NoConfirm

$localSoftExists = Test-Path $legacyLocalSoftPath
$localSoftForCloudExists = Test-Path $legacyLocalSoftForCloudPath
$auditFileList = @(Get-ChildItem $auditRootPath -File -Filter "audit-*.json" -ErrorAction SilentlyContinue)

if ($localSoftExists -or $localSoftForCloudExists) {
  throw "test7092 failed. legacy roots remain. localSoftExists=$localSoftExists localSoftForCloudExists=$localSoftForCloudExists"
}
if ($auditFileList.Count -eq 0) {
  throw "test7092 failed. audit log is not generated. auditRoot=$auditRootPath"
}

Write-Host "[7092] OK"
Write-Host "  auditFile=$($auditFileList[0].FullName)"
Write-Host "  temporaryRoot=$temporaryRootPath"
