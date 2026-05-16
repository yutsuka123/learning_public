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

function resolveNodeExecutablePath {
  <#
  .SYNOPSIS
  LocalServer 起動に使用する node.exe の実体パスを解決する。

  .DESCRIPTION
  [重要] Task Scheduler の非対話実行では PATH が対話シェルと異なることがあるため、
  `npm` ではなく `node dist/server.js` を直接起動できるよう、既知の候補から node.exe を探す。
  [厳守] 見つからない場合は候補一覧を含む詳細エラーで停止する。
  #>
  $nodePathList = New-Object System.Collections.Generic.List[string]

  $nodeCommand = Get-Command "node.exe" -ErrorAction SilentlyContinue
  if ($nodeCommand -and $nodeCommand.Source) {
    $nodePathList.Add($nodeCommand.Source)
  }

  $candidatePathList = @(
    (Join-Path $env:ProgramFiles "nodejs\node.exe"),
    "C:\nvm4w\nodejs\node_modules\nodejs\node.exe",
    "C:\Program Files\nodejs\node.exe"
  )

  foreach ($candidatePath in $candidatePathList) {
    if ([string]::IsNullOrWhiteSpace($candidatePath)) {
      continue
    }
    if ($nodePathList -notcontains $candidatePath) {
      $nodePathList.Add($candidatePath)
    }
  }

  foreach ($currentNodePath in $nodePathList) {
    if (Test-Path $currentNodePath) {
      return $currentNodePath
    }
  }

  $candidateSummary = ($nodePathList | ForEach-Object { "'$_'" }) -join ", "
  throw "resolveNodeExecutablePath failed. node.exe was not found. candidates=$candidateSummary"
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Resolve-Path (Join-Path $scriptDir "..")
$serverEntryPath = Join-Path $projectDir "dist\server.js"
$nodeExecutablePath = resolveNodeExecutablePath

Set-Location $projectDir
if (-not (Test-Path $serverEntryPath)) {
  throw "run-local-server.ps1 failed. dist/server.js is missing. projectDir=$projectDir serverEntryPath=$serverEntryPath"
}

Write-Host "[run-local-server] projectDir=$projectDir"
Write-Host "[run-local-server] nodeExecutablePath=$nodeExecutablePath"
Write-Host "[run-local-server] serverEntryPath=$serverEntryPath"

& $nodeExecutablePath $serverEntryPath
