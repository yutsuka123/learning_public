#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Create Windows Firewall inbound allow rules for IoT LAN, restricted to IoTSubnet.

.DESCRIPTION
    Creates inbound allow rules for LocalServer (3100/TCP, 4443/TCP), Mosquitto (8883/TCP),
    and CoreDNS (53/TCP+UDP) with RemoteAddress restricted to IoTSubnet.

    Legacy rules (mosquitto, coredns, "ota tcp4443", "IoT LocalServer OTA 4443") are
    set to Enabled=False rather than deleted, preserving rollback capability.

    The IoT network adapter name is resolved in this order:
      1. -IoTNetworkAdapterName parameter (if specified)
      2. Auto-detection: finds the adapter whose IPv4 address falls within IoTSubnet
      3. Skipped: if neither yields a result, profile change is skipped

.PARAMETER IoTSubnet
    IoT LAN subnet in CIDR notation. Default: "172.17.1.0/24".
    Must match the iotLanSubnet value in LocalServer settings.json.

.PARAMETER IoTNetworkAdapterName
    Name of the network adapter on the IoT LAN. Optional.
    When omitted, the adapter is auto-detected from IoTSubnet.

.PARAMETER Rollback
    When specified, removes the new rules and re-enables the legacy rules.

.EXAMPLE
    # Apply with explicit adapter name
    .\applyIoTFirewallRules.ps1 -IoTSubnet "172.17.1.0/24" -IoTNetworkAdapterName "Ethernet 2"

.EXAMPLE
    # Apply with auto-detection (recommended: no adapter name required)
    .\applyIoTFirewallRules.ps1 -IoTSubnet "172.17.1.0/24"

.EXAMPLE
    # Rollback
    .\applyIoTFirewallRules.ps1 -Rollback
#>
param(
    [string]$IoTSubnet = "172.17.1.0/24",
    [string]$IoTNetworkAdapterName = "",
    [switch]$Rollback
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ---- New rule definitions ----
$newRules = @(
    @{ Name = "IoT-Mosquitto-8883-In";     Protocol = "TCP"; LocalPort = "8883"; Description = "Mosquitto MQTT broker - IoT LAN only" },
    @{ Name = "IoT-CoreDNS-53-UDP-In";     Protocol = "UDP"; LocalPort = "53";   Description = "CoreDNS - IoT LAN only (UDP)" },
    @{ Name = "IoT-CoreDNS-53-TCP-In";     Protocol = "TCP"; LocalPort = "53";   Description = "CoreDNS - IoT LAN only (TCP)" },
    @{ Name = "IoT-LocalServer-4443-In";   Protocol = "TCP"; LocalPort = "4443"; Description = "LocalServer OTA - IoT LAN only" },
    @{ Name = "IoT-LocalServer-3100-In";   Protocol = "TCP"; LocalPort = "3100"; Description = "LocalServer API - IoT LAN + localhost" }
)

# Port 3100 also allows localhost access
$subnet3100 = @($IoTSubnet, "127.0.0.1")

# ---- Legacy rule names to disable ----
$legacyRuleNames = @("mosquitto", "coredns", "ota tcp4443", "IoT LocalServer OTA 4443")

# ====================================================================
# Helper: auto-detect IoT adapter by matching interface IP to CIDR subnet
# ====================================================================
function Get-IoTAdapterName {
    param([string]$Subnet)
    try {
        $parts = $Subnet -split '/'
        if ($parts.Count -ne 2) { return $null }
        $prefixLen = [int]$parts[1]
        if ($prefixLen -lt 0 -or $prefixLen -gt 32) { return $null }

        $mask = if ($prefixLen -eq 0) { [uint32]0 } `
                else { [uint32]([uint32]::MaxValue -shl (32 - $prefixLen)) }

        $netOctets = $parts[0] -split '\.'
        if ($netOctets.Count -ne 4) { return $null }
        $netUint = ([uint32]$netOctets[0] -shl 24) -bor ([uint32]$netOctets[1] -shl 16) `
                 -bor ([uint32]$netOctets[2] -shl 8) -bor [uint32]$netOctets[3]
        $netMasked = $netUint -band $mask

        $matched = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object {
            $addrOctets = $_.IPAddress -split '\.'
            if ($addrOctets.Count -ne 4) { return $false }
            $addrUint = ([uint32]$addrOctets[0] -shl 24) -bor ([uint32]$addrOctets[1] -shl 16) `
                      -bor ([uint32]$addrOctets[2] -shl 8) -bor [uint32]$addrOctets[3]
            ($addrUint -band $mask) -eq $netMasked
        } | Select-Object -First 1

        if ($matched) { return $matched.InterfaceAlias }
    } catch { }
    return $null
}

# ====================================================================
# Rollback
# ====================================================================
if ($Rollback) {
    Write-Host "[Rollback] Removing new rules and re-enabling legacy rules..."
    foreach ($r in $newRules) {
        $existing = Get-NetFirewallRule -DisplayName $r.Name -ErrorAction SilentlyContinue
        if ($existing) {
            Remove-NetFirewallRule -DisplayName $r.Name
            Write-Host "  Removed: $($r.Name)"
        } else {
            Write-Host "  Skip (not found): $($r.Name)"
        }
    }
    foreach ($legacyName in $legacyRuleNames) {
        $existing = Get-NetFirewallRule -DisplayName $legacyName -ErrorAction SilentlyContinue
        if ($existing) {
            Set-NetFirewallRule -DisplayName $legacyName -Enabled True
            Write-Host "  Re-enabled: $legacyName"
        } else {
            Write-Host "  Skip (not found): $legacyName"
        }
    }
    Write-Host "[Rollback] Done."
    return
}

# ====================================================================
# Apply
# ====================================================================
Write-Host "[Apply] IoTSubnet=$IoTSubnet"

# ---- Resolve adapter name ----
$resolvedAdapterName = $IoTNetworkAdapterName
if ($resolvedAdapterName -eq "") {
    $detected = Get-IoTAdapterName -Subnet $IoTSubnet
    if ($detected) {
        $resolvedAdapterName = $detected
        Write-Host "[Apply] Auto-detected IoT adapter: $resolvedAdapterName"
    } else {
        Write-Warning "[Apply] Could not auto-detect IoT adapter for subnet $IoTSubnet. Network profile change will be skipped."
    }
}

# ---- Network profile change (Private) ----
if ($resolvedAdapterName -ne "") {
    try {
        $profile = Get-NetConnectionProfile -InterfaceAlias $resolvedAdapterName -ErrorAction Stop
        if ($profile.NetworkCategory -ne "Private") {
            Set-NetConnectionProfile -InterfaceAlias $resolvedAdapterName -NetworkCategory Private
            Write-Host "  Network profile changed: $resolvedAdapterName -> Private"
        } else {
            Write-Host "  Network profile already Private: $resolvedAdapterName"
        }
    } catch {
        Write-Warning "  Profile change skipped (adapter not found): $resolvedAdapterName"
    }
}

# ---- Disable legacy rules ----
Write-Host "[Legacy rules] Disabling..."
foreach ($legacyName in $legacyRuleNames) {
    $existing = Get-NetFirewallRule -DisplayName $legacyName -ErrorAction SilentlyContinue
    if ($existing) {
        Set-NetFirewallRule -DisplayName $legacyName -Enabled False
        Write-Host "  Disabled: $legacyName"
    } else {
        Write-Host "  Skip (not found): $legacyName"
    }
}

# ---- Create or update new rules ----
Write-Host "[New rules] Applying..."
foreach ($r in $newRules) {
    $existing = Get-NetFirewallRule -DisplayName $r.Name -ErrorAction SilentlyContinue
    if ($existing) {
        Write-Host "  Update: $($r.Name)"
        Set-NetFirewallRule -DisplayName $r.Name -Enabled True -Profile Private -RemoteAddress $IoTSubnet
        if ($r.LocalPort -eq "3100") {
            Set-NetFirewallRule -DisplayName $r.Name -RemoteAddress $subnet3100
        }
    } else {
        Write-Host "  Create: $($r.Name) port=$($r.LocalPort)/$($r.Protocol)"
        $remoteAddr = if ($r.LocalPort -eq "3100") { $subnet3100 } else { @($IoTSubnet) }
        New-NetFirewallRule `
            -DisplayName $r.Name `
            -Direction Inbound `
            -Action Allow `
            -Protocol $r.Protocol `
            -LocalPort $r.LocalPort `
            -RemoteAddress $remoteAddr `
            -Profile Private `
            -Description $r.Description `
            -Enabled True | Out-Null
    }
}

# ---- Result summary ----
Write-Host ""
Write-Host "[Result] Applied rules:"
foreach ($r in $newRules) {
    $rule = Get-NetFirewallRule -DisplayName $r.Name -ErrorAction SilentlyContinue
    if ($rule) {
        $addrFilter = $rule | Get-NetFirewallAddressFilter
        Write-Host ("  {0,-40} Profile={1,-8} RemoteAddr={2}" -f $r.Name, $rule.Profile, ($addrFilter.RemoteAddress -join ","))
    } else {
        Write-Warning "  Rule not found: $($r.Name)"
    }
}

Write-Host ""
Write-Host "[Done] IoT Firewall rules applied."
