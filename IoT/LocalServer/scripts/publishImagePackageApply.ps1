<#
[重要]
- 本スクリプトは imagePackageApply の署名付き payload 生成と MQTT publish を一括実行する。
- 署名計算は `scripts/generateImagePackageApplyPayload.mjs` に委譲し、ESP32側検証ロジックと整合する。

[厳守]
- k-device は Base64 32byte 復号できる値を指定すること。
- destinationDir は `/images` 配下を指定すること（`/logs` 禁止）。
- 機密値（MQTTパスワード、k-device）はログ共有しないこと。

[制限]
- `mosquitto_pub` が OS PATH に存在する環境を前提とする。
- `-DryRun` 指定時は publish せず、生成 payload のみ表示する。
#>

param(
  [Parameter(Mandatory = $true)]
  [string]$ReceiverName,

  [Parameter(Mandatory = $true)]
  [string]$KDeviceBase64,

  [Parameter(Mandatory = $false)]
  [string]$SourceId = "server-001",

  [Parameter(Mandatory = $false)]
  [string]$PackageUrl = "https://mqtt.esplab.home.arpa:4443/assets/images-pack.zip",

  [Parameter(Mandatory = $true)]
  [string]$PackageSha256,

  [Parameter(Mandatory = $false)]
  [string]$DestinationDir = "/images/top",

  [Parameter(Mandatory = $false)]
  [bool]$Overwrite = $true,

  [Parameter(Mandatory = $false)]
  [string]$MqttHost = "mqtt.esplab.home.arpa",

  [Parameter(Mandatory = $false)]
  [int]$MqttPort = 8883,

  [Parameter(Mandatory = $false)]
  [string]$MqttUsername = "",

  [Parameter(Mandatory = $false)]
  [securestring]$MqttPassword,

  [Parameter(Mandatory = $false)]
  [string]$CaFilePath = "",

  [Parameter(Mandatory = $false)]
  [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-GeneratedPayloadResult {
  param(
    [string]$scriptPath
  )
  $overwriteText = if ($Overwrite) { "true" } else { "false" }
  $nodeCommandOutput = node $scriptPath `
    --receiverName $ReceiverName `
    --sourceId $SourceId `
    --packageUrl $PackageUrl `
    --packageSha256 $PackageSha256 `
    --destinationDir $DestinationDir `
    --overwrite $overwriteText `
    --kDeviceBase64 $KDeviceBase64

  if ($LASTEXITCODE -ne 0) {
    throw "Get-GeneratedPayloadResult failed. generate script exited with code=$LASTEXITCODE"
  }

  $topicLine = $nodeCommandOutput | Where-Object { $_ -like "TOPIC=*" } | Select-Object -First 1
  $payloadLine = $nodeCommandOutput | Where-Object { $_ -like "PAYLOAD=*" } | Select-Object -First 1
  if ([string]::IsNullOrWhiteSpace($topicLine) -or [string]::IsNullOrWhiteSpace($payloadLine)) {
    throw "Get-GeneratedPayloadResult failed. TOPIC/PAYLOAD line is missing."
  }

  return @{
    Topic = $topicLine.Substring("TOPIC=".Length)
    Payload = $payloadLine.Substring("PAYLOAD=".Length)
    RawOutput = $nodeCommandOutput
  }
}

function Publish-ImagePackageApply {
  param(
    [string]$topic,
    [string]$payload
  )
  $mosquittoCommand = Get-Command "mosquitto_pub" -ErrorAction SilentlyContinue
  if ($null -eq $mosquittoCommand) {
    throw "Publish-ImagePackageApply failed. mosquitto_pub is not found in PATH."
  }

  $publishArguments = @(
    "-h", $MqttHost,
    "-p", "$MqttPort",
    "-t", $topic,
    "-m", $payload
  )
  if (-not [string]::IsNullOrWhiteSpace($MqttUsername)) {
    $publishArguments += @("-u", $MqttUsername)
  }
  $mqttPasswordPlainText = ""
  if ($null -ne $MqttPassword) {
    $credential = New-Object System.Management.Automation.PSCredential("mqtt-user", $MqttPassword)
    $mqttPasswordPlainText = $credential.GetNetworkCredential().Password
  }
  if (-not [string]::IsNullOrWhiteSpace($mqttPasswordPlainText)) {
    $publishArguments += @("-P", $mqttPasswordPlainText)
  }
  if (-not [string]::IsNullOrWhiteSpace($CaFilePath)) {
    $publishArguments += @("--cafile", $CaFilePath)
  }

  & mosquitto_pub @publishArguments
  if ($LASTEXITCODE -ne 0) {
    throw "Publish-ImagePackageApply failed. mosquitto_pub exited with code=$LASTEXITCODE"
  }
}

try {
  $currentScriptDirectory = Split-Path -Parent $PSCommandPath
  $generatorScriptPath = Join-Path $currentScriptDirectory "generateImagePackageApplyPayload.mjs"
  if (-not (Test-Path -Path $generatorScriptPath)) {
    throw "generator script not found. path=$generatorScriptPath"
  }

  $generatedResult = Get-GeneratedPayloadResult -scriptPath $generatorScriptPath
  Write-Host "[INFO] TOPIC=$($generatedResult.Topic)"
  Write-Host "[INFO] PAYLOAD=$($generatedResult.Payload)"

  if ($DryRun.IsPresent) {
    Write-Host "[INFO] DryRun mode: skip mosquitto_pub."
    exit 0
  }

  Publish-ImagePackageApply -topic $generatedResult.Topic -payload $generatedResult.Payload
  Write-Host "[INFO] publish completed."
  exit 0
} catch {
  Write-Error $_.Exception.Message
  exit 1
}

