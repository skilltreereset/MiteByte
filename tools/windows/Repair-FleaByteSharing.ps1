#requires -Version 5.1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'FleaByte-Sharing.ps1')

function Repair-SharingConnection($Plan) {
    if (-not $Plan.Ready) { throw $Plan.Message }
    # Get-SharingPlan validates the USB identity and refuses another private
    # sharing destination. Refresh only an existing FleaByte sharing pair.
    if (-not ($Plan.Private.Enabled -and $Plan.Private.Type -eq 1 -and
              $Plan.Public.Enabled -and $Plan.Public.Type -eq 0)) {
        Enable-FleaByteSharing $Plan
        return
    }
    Ensure-SharingService
    try {
        $Plan.Private.Configuration.DisableSharing()
        $Plan.Public.Configuration.DisableSharing()
        $Plan.Public.Configuration.EnableSharing(0)
        $Plan.Private.Configuration.EnableSharing(1)
        Ensure-SharingService
    } catch {
        $failure = $_
        try {
            $Plan.Public.Configuration.EnableSharing(0)
            $Plan.Private.Configuration.EnableSharing(1)
        } catch { Write-Warning 'Windows could not restore the prior sharing pair.' }
        throw $failure
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    if (-not (Test-Administrator)) { throw 'Run this repair in Windows PowerShell as administrator. Leave FleaByte Wi-Fi Hotspot running.' }
    $plan = Get-SharingPlan
    Repair-SharingConnection $plan
    Write-Host 'FleaByte Windows sharing configuration reapplied. Leave the hotspot running while it retries DHCP.'
    Write-Host 'Check the USB address and test a website. This message confirms configuration, not internet connectivity.'
}
