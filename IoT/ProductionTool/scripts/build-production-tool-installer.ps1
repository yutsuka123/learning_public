<#
.SYNOPSIS
ProductionTool 用インストーラ EXE をビルドする。

.DESCRIPTION
[009-1021][厳守] `cargo build --release` で `production_tool.exe` を生成し、Inno Setup (`ISCC.exe`) で `ProductionToolSetup.exe` を作成します。
[重要] インストーラは `installer/ProductionTool.iss` を正とし、配布前提は `インストーラEXE化設計仕様書.md` 3.4 に従います。
[禁止] 手動コピーだけで配布物を作らず、ビルド手順は本スクリプトまたは同等の自動化で固定します。

.PARAMETER Configuration
Cargo のビルド構成です。既定値は `release` です。

.PARAMETER SkipCargoBuild
Cargo ビルドをスキップします。既に `target\release\production_tool.exe` がある場合のみ使用します。

.PARAMETER IsccPath
`ISCC.exe` のフルパスです。未指定時は `PATH` 上の `ISCC.exe` または代表的な Inno Setup パスを探索します。

.EXAMPLE
.\build-production-tool-installer.ps1

.EXAMPLE
.\build-production-tool-installer.ps1 -SkipCargoBuild -IsccPath "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
#>

param(
  [ValidateSet("release")]
  [string]$Configuration = "release",
  [switch]$SkipCargoBuild,
  [string]$IsccPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRootPath = Resolve-Path (Join-Path $scriptDir "..")
$installerScriptPath = Join-Path $projectRootPath "installer\ProductionTool.iss"
$cargoOutputDirectoryPath = Join-Path $projectRootPath "target\$Configuration"
$binaryFilePath = Join-Path $cargoOutputDirectoryPath "production_tool.exe"

function resolveIsccPath {
  param([string]$explicitPath)

  if ($explicitPath) {
    if (-not (Test-Path $explicitPath)) {
      throw "resolveIsccPath failed. explicitPath not found. explicitPath=$explicitPath"
    }
    return (Resolve-Path $explicitPath).Path
  }

  $pathCommand = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
  if ($null -ne $pathCommand) {
    return $pathCommand.Source
  }

  $candidatePathList = @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe"
  )

  foreach ($candidatePath in $candidatePathList) {
    if (Test-Path $candidatePath) {
      return $candidatePath
    }
  }

  throw "resolveIsccPath failed. ISCC.exe is not found. hint=install Inno Setup 6 or pass -IsccPath."
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

if (-not (Test-Path $installerScriptPath)) {
  throw "build-production-tool-installer failed. installer script is not found. path=$installerScriptPath"
}

if (-not $SkipCargoBuild) {
  invokeProcessChecked `
    -filePath "cargo" `
    -argumentList @("build", "--release") `
    -workingDirectoryPath $projectRootPath
}

if (-not (Test-Path $binaryFilePath)) {
  throw "build-production-tool-installer failed. production_tool.exe is not found. path=$binaryFilePath"
}

$resolvedIsccPath = resolveIsccPath -explicitPath $IsccPath

invokeProcessChecked `
  -filePath $resolvedIsccPath `
  -argumentList @($installerScriptPath) `
  -workingDirectoryPath (Split-Path -Parent $installerScriptPath)

Write-Host "[完了] ProductionTool installer build finished."
Write-Host "[出力想定] installer\build\ProductionToolSetup.exe"
