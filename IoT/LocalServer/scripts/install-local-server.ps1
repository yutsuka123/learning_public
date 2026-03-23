<#
.SYNOPSIS
LocalServer をインストールする。

.DESCRIPTION
[重要] 003-0015 で整備。依存導入・ビルド・作業ディレクトリ作成を行う。
[厳守] 実行ユーザーに Node.js 実行権限があること。
[禁止] このスクリプトへ機密情報を直書きしない。

.PARAMETER InstallTaskScheduler
Task Scheduler へ自動起動タスクを登録する。未指定時は登録しない。
#>

param(
    [switch]$InstallTaskScheduler
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Resolve-Path (Join-Path $scriptDir "..")

Write-Host "[install-local-server] Installing LocalServer. projectDir=$projectDir"

Set-Location $projectDir

# 依存導入
if (-not (Test-Path "node_modules")) {
    Write-Host "[install-local-server] Running npm install..."
    npm install
} else {
    Write-Host "[install-local-server] node_modules exists. Skipping npm install. (Use npm install manually to update.)"
}

# ビルド
Write-Host "[install-local-server] Running npm run build..."
npm run build

# data / logs / uploads ディレクトリ作成
$dirs = @("data", "logs", "uploads")
foreach ($d in $dirs) {
    $path = Join-Path $projectDir $d
    if (-not (Test-Path $path)) {
        New-Item -ItemType Directory -Path $path -Force | Out-Null
        Write-Host "[install-local-server] Created directory: $d"
    }
}

# certs は証明書配置用。存在しなくても起動は可能（OTA HTTPS は無効化される）
$certsPath = Join-Path $projectDir "certs"
if (-not (Test-Path $certsPath)) {
    New-Item -ItemType Directory -Path $certsPath -Force | Out-Null
    Write-Host "[install-local-server] Created directory: certs (place server.crt, server.key for OTA HTTPS)"
}

if ($InstallTaskScheduler) {
    Write-Host "[install-local-server] Registering Task Scheduler..."
    $taskScript = Join-Path $scriptDir "install-task-scheduler.ps1"
    & $taskScript
}

Write-Host "[install-local-server] Install completed. Run: cd $projectDir; npm run start"
