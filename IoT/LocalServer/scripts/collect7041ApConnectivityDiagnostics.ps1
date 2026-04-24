<#
.SYNOPSIS
7041 AP 到達性トラブルの証跡を一括採取する診断スクリプト。

.DESCRIPTION
[重要] `test:7041` / `test:7041:pipeline` が `ap_unreachable` で失敗した直後に、
Windows 側の無線状態・IP経路・ARP・疎通・WLANイベントをまとめて保存する。
[厳守] 証跡は必ずタイムスタンプ付きフォルダへ保存し、試験記録書へ参照パスを残す。
[禁止] 一時的なセキュリティ緩和（認証無効化・暗号無効化）を目的にしない。
[推奨] `--interfaceName` と `--ssidName` を指定して、対象APに絞って採取する。

主な採取項目:
- `ipconfig` / `route print` / `arp -a`
- `netsh wlan show interfaces|drivers|profiles`
- `ping` / `curl`（`http://<targetHost>/api/auth/login`）
- `Microsoft-Windows-WLAN-AutoConfig/Operational` イベント抽出

制限事項:
- 管理者権限がない場合、IPv4設定変更系コマンドは実行しない（本スクリプト内では未実施）。
- `curl` の結果は到達性確認目的のみで、認証情報は送信しない。
- 日本語OSのイベント本文は環境依存文字化けの可能性があるため、構造化JSONも併せて出力する。

.PARAMETER interfaceName
採取対象の無線インターフェイス名（例: `Wi-Fi_lab`）。

.PARAMETER ssidName
対象SSID名（例: `AP-esp32lab-F0D0F94EB580`）。

.PARAMETER targetHost
APゲートウェイ/到達確認先ホスト（既定: `192.168.4.1`）。

.PARAMETER outputRoot
診断結果の保存先ルート。未指定時は `LocalServer/logs/net-diagnostics`。

.PARAMETER eventLookbackHours
WLANイベントを遡って採取する時間（時間単位）。

.PARAMETER pingCount
疎通確認 ping の送信回数。

.EXAMPLE
powershell -ExecutionPolicy Bypass -File ".\scripts\collect7041ApConnectivityDiagnostics.ps1"

.EXAMPLE
powershell -ExecutionPolicy Bypass -File ".\scripts\collect7041ApConnectivityDiagnostics.ps1" -interfaceName "Wi-Fi_lab" -ssidName "AP-esp32lab-F0D0F94EB580" -targetHost "192.168.4.1"
#>

param(
  [string]$interfaceName = "Wi-Fi_lab",
  [string]$ssidName = "AP-esp32lab-F0D0F94EB580",
  [string]$targetHost = "192.168.4.1",
  [string]$outputRoot = "",
  [int]$eventLookbackHours = 24,
  [int]$pingCount = 5
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($outputRoot)) {
  $scriptDirectoryPath = Split-Path -Parent $MyInvocation.MyCommand.Path
  $outputRoot = Join-Path (Split-Path -Parent $scriptDirectoryPath) "logs\net-diagnostics"
}

$runTimestampText = (Get-Date).ToString("yyyyMMdd-HHmmss")
$runDirectoryPath = Join-Path $outputRoot ("7041-apdiag-{0}" -f $runTimestampText)
New-Item -ItemType Directory -Path $runDirectoryPath -Force | Out-Null

function Save-Text {
  param(
    [string]$fileName,
    [string]$content
  )

  $targetPath = Join-Path $runDirectoryPath $fileName
  [System.IO.File]::WriteAllText($targetPath, $content, [System.Text.UTF8Encoding]::new($false))
  return $targetPath
}

function Invoke-DiagnosticCommand {
  param(
    [string]$commandName,
    [string]$commandLine
  )

  try {
    Write-Host "[apdiag] start: $commandName"
    $outputText = & cmd /c $commandLine 2>&1 | Out-String
    $savedPath = Save-Text -fileName ("{0}.txt" -f $commandName) -content $outputText
    Write-Host "[apdiag] saved: $savedPath"
  } catch {
    $errorMessage = "Invoke-DiagnosticCommand failed. commandName={0} commandLine={1} detail={2}" -f $commandName, $commandLine, $_.Exception.Message
    Save-Text -fileName ("{0}.error.txt" -f $commandName) -content $errorMessage | Out-Null
    throw $errorMessage
  }
}

function Export-WlanEvents {
  param(
    [string]$targetInterfaceName,
    [string]$targetSsidName,
    [int]$lookbackHours
  )

  try {
    $startDateTime = (Get-Date).AddHours(-1 * $lookbackHours)
    $eventList = Get-WinEvent -FilterHashtable @{
      LogName = "Microsoft-Windows-WLAN-AutoConfig/Operational"
      StartTime = $startDateTime
    } -ErrorAction Stop

    $filteredEventList = @(
      $eventList | Where-Object {
        $_.Message -match [regex]::Escape($targetInterfaceName) -or
        $_.Message -match [regex]::Escape($targetSsidName)
      } | Select-Object -First 500
    )

    $eventObjectList = foreach ($eventItem in $filteredEventList) {
      $eventXml = [xml]$eventItem.ToXml()
      $eventDataMap = @{}
      foreach ($eventDataItem in $eventXml.Event.EventData.Data) {
        if (-not [string]::IsNullOrWhiteSpace($eventDataItem.Name)) {
          $eventDataMap[$eventDataItem.Name] = [string]$eventDataItem.InnerText
        }
      }

      [PSCustomObject]@{
        time = $eventItem.TimeCreated.ToString("o")
        id = $eventItem.Id
        level = $eventItem.LevelDisplayName
        provider = $eventItem.ProviderName
        data = $eventDataMap
        message = $eventItem.Message
      }
    }
    $eventObjectListArray = @($eventObjectList)

    $eventJsonPath = Join-Path $runDirectoryPath "wlan-events.json"
    $eventObjectListArray | ConvertTo-Json -Depth 8 | Set-Content -Path $eventJsonPath -Encoding UTF8

    $eventSummaryObject = [PSCustomObject]@{
      # [重要][修正理由] 単一イベント時は PSCustomObject に Count プロパティが生えないため、
      #                配列化してから件数を読む。ここで落ちると診断証跡そのものが保存できない。
      totalCount = $eventObjectListArray.Count
      idCount = @(
        $eventObjectListArray |
          Group-Object -Property id |
          Sort-Object -Property Count -Descending |
          ForEach-Object {
            [PSCustomObject]@{
              id = [int]$_.Name
              count = $_.Count
            }
          }
      )
      range = [PSCustomObject]@{
        from = $startDateTime.ToString("o")
        to = (Get-Date).ToString("o")
      }
      interfaceName = $targetInterfaceName
      ssidName = $targetSsidName
    }
    $eventSummaryObject | ConvertTo-Json -Depth 8 | Set-Content -Path (Join-Path $runDirectoryPath "wlan-events-summary.json") -Encoding UTF8

    Write-Host "[apdiag] saved: $eventJsonPath"
  } catch {
    $errorMessage = "Export-WlanEvents failed. interfaceName={0} ssidName={1} lookbackHours={2} detail={3}" -f $targetInterfaceName, $targetSsidName, $lookbackHours, $_.Exception.Message
    Save-Text -fileName "wlan-events.error.txt" -content $errorMessage | Out-Null
    throw $errorMessage
  }
}

try {
  $runContextObject = [PSCustomObject]@{
    generatedAt = (Get-Date).ToString("o")
    interfaceName = $interfaceName
    ssidName = $ssidName
    targetHost = $targetHost
    eventLookbackHours = $eventLookbackHours
    pingCount = $pingCount
    executedBy = $env:USERNAME
    machineName = $env:COMPUTERNAME
    scriptPath = $MyInvocation.MyCommand.Path
    runDirectoryPath = $runDirectoryPath
  }
  $runContextObject | ConvertTo-Json -Depth 5 | Set-Content -Path (Join-Path $runDirectoryPath "context.json") -Encoding UTF8

  Invoke-DiagnosticCommand -commandName "wlan-show-interfaces" -commandLine "netsh wlan show interfaces"
  Invoke-DiagnosticCommand -commandName "wlan-show-drivers-interface" -commandLine ("netsh wlan show drivers interface=""{0}""" -f $interfaceName)
  Invoke-DiagnosticCommand -commandName "wlan-show-profile-interface" -commandLine ("netsh wlan show profile name=""{0}"" interface=""{1}"" key=clear" -f $ssidName, $interfaceName)
  Invoke-DiagnosticCommand -commandName "ipconfig-all" -commandLine "ipconfig /all"
  Invoke-DiagnosticCommand -commandName "route-print" -commandLine "route print"
  Invoke-DiagnosticCommand -commandName "arp-all" -commandLine "arp -a"
  Invoke-DiagnosticCommand -commandName "ping-target-host" -commandLine ("ping -n {0} {1}" -f $pingCount, $targetHost)
  Invoke-DiagnosticCommand -commandName "curl-auth-login" -commandLine ("curl.exe -m 6 -s -S -D - -o NUL -w ""http_code=%{{http_code}}\n"" http://{0}/api/auth/login" -f $targetHost)

  Export-WlanEvents -targetInterfaceName $interfaceName -targetSsidName $ssidName -lookbackHours $eventLookbackHours

  Write-Host "[apdiag] completed. outputDir=$runDirectoryPath"
} catch {
  $failureMessage = "collect7041ApConnectivityDiagnostics failed. interfaceName={0} ssidName={1} targetHost={2} detail={3}" -f $interfaceName, $ssidName, $targetHost, $_.Exception.Message
  Save-Text -fileName "fatal.error.txt" -content $failureMessage | Out-Null
  throw $failureMessage
}
