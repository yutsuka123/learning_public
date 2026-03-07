<#
.SYNOPSIS
LocalServerを起動するラッパースクリプト。

.DESCRIPTION
[重要] Task Schedulerから実行する際に作業ディレクトリを固定するための起動スクリプト。
[厳守] 実行前に依存導入とbuild完了状態であること。
[禁止] このスクリプトへ機密情報を直書きしない。
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Resolve-Path (Join-Path $scriptDir "..")

Set-Location $projectDir
npm run start
