param()
$ErrorActionPreference = "Stop"

$AP_SSID    = 'AP-esp32lab-04CEF94EB580'
$AP_IFACE   = 'Wi-Fi_lab'
$ESP32_BASE = 'http://192.168.4.1'
$MAINT_USER = 'maintenance'
$MAINT_PASS = 'esp32m1732'
$CLOUD_EP   = 'a18obaz8zsc0rk-ats.iot.ap-northeast-1.amazonaws.com'

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host 'Relaunching as Administrator...'
    Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    exit 0
}

Write-Host '=== ESP32 Cloud Mode Setup ==='

Write-Host "[1/6] Set $AP_IFACE to DHCP..."
netsh interface ip set address $AP_IFACE dhcp | Out-Null
netsh interface ip set dns $AP_IFACE dhcp | Out-Null
Start-Sleep -Seconds 2

Write-Host "[2/6] Connecting to $AP_SSID ..."
netsh wlan connect name=$AP_SSID ssid=$AP_SSID interface=$AP_IFACE | Out-Null

Write-Host '[3/6] Waiting for 192.168.4.x IP (max 60s)...'
$clientIp = $null
$i = 0
while ($i -lt 30 -and -not $clientIp) {
    Start-Sleep -Seconds 2
    $addrs = ipconfig | Out-String
    if ($addrs -match '192\.168\.4\.(\d+)') {
        $clientIp = "192.168.4.$($Matches[1])"
        Write-Host "    Got IP: $clientIp"
    } else {
        $wlan = netsh wlan show interfaces | Out-String
        if ($wlan -notmatch 'connected') {
            netsh wlan connect name=$AP_SSID ssid=$AP_SSID interface=$AP_IFACE | Out-Null
        }
        if (($i % 5) -eq 4) { Write-Host "    Waiting... $([int]($i*2+2))s" }
        $i++
    }
}
if (-not $clientIp) {
    Write-Host 'ERROR: Could not get 192.168.4.x IP'
    Read-Host 'Press Enter to exit'
    exit 1
}

Write-Host '[4/6] Login to ESP32 AP...'
Start-Sleep -Seconds 1
$loginBody = '{"username":"' + $MAINT_USER + '","password":"' + $MAINT_PASS + '"}'
try {
    $loginResp = Invoke-RestMethod -Uri "$ESP32_BASE/api/auth/login" `
        -Method POST -ContentType 'application/json' `
        -Body $loginBody -TimeoutSec 10
    $token = $loginResp.token
    Write-Host '    Login OK'
} catch {
    Write-Host "ERROR: Login failed: $_"
    Read-Host 'Press Enter to exit'
    exit 1
}

Write-Host '[5/6] Set brokerMode=cloud...'
$settingsBody = '{"brokerMode":"cloud","cloudEndpoint":"' + $CLOUD_EP + '"}'
try {
    $resp = Invoke-RestMethod -Uri "$ESP32_BASE/api/settings/network" `
        -Method POST -ContentType 'application/json' `
        -Headers @{ Authorization = "Bearer $token" } `
        -Body $settingsBody -TimeoutSec 10
    Write-Host '    Settings OK'
    Write-Host ($resp | ConvertTo-Json -Compress)
} catch {
    Write-Host "ERROR: Settings failed: $_"
    Read-Host 'Press Enter to exit'
    exit 1
}

Write-Host '[6/6] Rebooting ESP32...'
try {
    Invoke-RestMethod -Uri "$ESP32_BASE/api/system/reboot" `
        -Method POST -ContentType 'application/json' `
        -Headers @{ Authorization = "Bearer $token" } `
        -Body '{}' -TimeoutSec 5 | Out-Null
    Write-Host '    Reboot command sent'
} catch {
    Write-Host '    (disconnect is normal - rebooting)'
}

Write-Host ''
Write-Host '=== DONE ==='
Write-Host 'ESP32 will restart in cloud mode.'
Write-Host 'Check LocalServer UI in ~15 seconds.'
Read-Host 'Press Enter to exit'
