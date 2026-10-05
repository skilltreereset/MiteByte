#requires -Version 5.1
<#
FleaByte Internet Sharing -- Windows 10/11, optional companion.
Install once as administrator. The named startup task configures only Windows
Internet Connection Sharing for a connected FleaByte USB Hotspot. It has no
command listener, password collection, screen capture or remote control.
Run -Check for a read-only diagnosis, -Uninstall to remove it, or configure
sharing manually in Windows and omit this companion entirely.
#>
[CmdletBinding()]
param(
    [switch]$Install, [switch]$Uninstall, [switch]$Watch, [switch]$Check,
    [string]$SourcePath
)
$ErrorActionPreference = 'Stop'
$script:TaskName = 'FleaByte Internet Sharing'
$script:InstallRoot = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'FleaByte-Sharing'
$script:InstalledScript = Join-Path $script:InstallRoot 'FleaByte-Sharing.ps1'
$script:LastMessage = ''

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    return ([Security.Principal.WindowsPrincipal]$identity).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-FleaByteAdapter($Adapter) {
    $instance = [string]$Adapter.PnPDeviceID
    if ($instance -notlike 'USB\*') { return $false }
    # Network interface -> USB composite parent. Match our firmware's product
    # and mode-specific serial, never whichever adapter happens to be USB.
    for ($depth = 0; $depth -lt 5 -and $instance; ++$depth) {
        $description = Get-PnpDeviceProperty -InstanceId $instance `
            -KeyName 'DEVPKEY_Device_BusReportedDeviceDesc' -ErrorAction SilentlyContinue
        if ($description.Data -eq 'FleaByte USB Hotspot' -and $instance -match '-H$') {
            return $true
        }
        $parent = Get-PnpDeviceProperty -InstanceId $instance `
            -KeyName 'DEVPKEY_Device_Parent' -ErrorAction SilentlyContinue
        $instance = [string]$parent.Data
    }
    return $false
}

function Select-Upstream($Routes, $Interfaces, $Adapters, [int]$UsbIndex) {
    $candidates = foreach ($route in $Routes) {
        if ($route.InterfaceIndex -eq $UsbIndex -or $route.DestinationPrefix -ne '0.0.0.0/0') { continue }
        $adapter = $Adapters | Where-Object { $_.InterfaceIndex -eq $route.InterfaceIndex -and $_.Status -eq 'Up' } | Select-Object -First 1
        $interface = $Interfaces | Where-Object { $_.InterfaceIndex -eq $route.InterfaceIndex } | Select-Object -First 1
        if ($null -eq $adapter -or $null -eq $interface) { continue }
        [pscustomobject]@{
            Adapter = $adapter
            Metric = [int]$route.RouteMetric + [int]$interface.InterfaceMetric
            Index = [int]$route.InterfaceIndex
        }
    }
    $selected = $candidates | Sort-Object Metric, Index | Select-Object -First 1
    if ($null -ne $selected) { return $selected.Adapter }
    return $null
}

function Get-SharingConnections($Manager) {
    foreach ($connection in $Manager.EnumEveryConnection) {
        $properties = $Manager.NetConnectionProps($connection)
        $configuration = $Manager.INetSharingConfigurationForINetConnection($connection)
        [pscustomobject]@{
            Guid = ([guid]$properties.Guid).ToString()
            Name = [string]$properties.Name
            Configuration = $configuration
            Enabled = [bool]$configuration.SharingEnabled
            Type = if ($configuration.SharingEnabled) { [int]$configuration.SharingConnectionType } else { -1 }
        }
    }
}

function Test-SharingConflict($Connections, [string]$UsbGuid) {
    return @($Connections | Where-Object { $_.Enabled -and $_.Type -eq 1 -and $_.Guid -ne $UsbGuid }).Count -gt 0
}

function Get-SharingPlan {
    $adapters = @(Get-NetAdapter -IncludeHidden)
    $dongles = @($adapters | Where-Object { Test-FleaByteAdapter $_ })
    if ($dongles.Count -ne 1 -or $dongles[0].Status -ne 'Up') {
        return [pscustomobject]@{ Ready = $false; Message = if ($dongles.Count -gt 1) {
            'Connect one FleaByte hotspot at a time.'
        } else { 'Waiting for FleaByte. Start Wi-Fi Hotspot in its library.' } }
    }
    $usb = $dongles[0]
    $source = Select-Upstream @(Get-NetRoute -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0') `
        @(Get-NetIPInterface -AddressFamily IPv4) $adapters $usb.InterfaceIndex
    if ($null -eq $source) { return [pscustomobject]@{ Ready = $false; Message = 'Waiting for the PC internet connection.' } }
    $manager = New-Object -ComObject HNetCfg.HNetShare
    $connections = @(Get-SharingConnections $manager)
    $usbGuid = ([guid]$usb.InterfaceGuid).ToString()
    $sourceGuid = ([guid]$source.InterfaceGuid).ToString()
    if (Test-SharingConflict $connections $usbGuid) {
        return [pscustomobject]@{ Ready = $false; Message = 'Another Windows sharing connection is active. It has been left unchanged.' }
    }
    $private = $connections | Where-Object Guid -eq $usbGuid | Select-Object -First 1
    $public = $connections | Where-Object Guid -eq $sourceGuid | Select-Object -First 1
    if ($null -eq $private -or $null -eq $public) {
        return [pscustomobject]@{ Ready = $false; Message = 'Windows has not finished registering the network adapters.' }
    }
    return [pscustomobject]@{
        Ready = $true; Message = ('Sharing ' + $source.Name + ' with FleaByte.')
        Private = $private; Public = $public; Connections = $connections
    }
}

function Ensure-SharingService {
    $service = Get-Service -Name SharedAccess
    if ($service.Status -eq 'Running') { return }
    # ICS settings survive USB removal, but its DHCP/DNS/NAT service may stop.
    # The approved SYSTEM task can start it without another UAC prompt.
    Start-Service -Name SharedAccess
    $service.WaitForStatus('Running', [TimeSpan]::FromSeconds(10))
}

function Enable-FleaByteSharing($Plan) {
    $oldPublic = $Plan.Connections | Where-Object { $_.Enabled -and $_.Type -eq 0 } | Select-Object -First 1
    $privateWasEnabled = $Plan.Private.Enabled -and $Plan.Private.Type -eq 1
    $publicWasEnabled = $Plan.Public.Enabled -and $Plan.Public.Type -eq 0
    if ($privateWasEnabled -and $publicWasEnabled) { Ensure-SharingService; return }
    try {
        if (-not $publicWasEnabled) { $Plan.Public.Configuration.EnableSharing(0) }
        if (-not $privateWasEnabled) { $Plan.Private.Configuration.EnableSharing(1) }
        Ensure-SharingService
    } catch {
        # Recover the prior sharing setup if Windows rejects the change.
        try {
            if (-not $privateWasEnabled -and $Plan.Private.Configuration.SharingEnabled) { $Plan.Private.Configuration.DisableSharing() }
            if (-not $publicWasEnabled -and $Plan.Public.Configuration.SharingEnabled) { $Plan.Public.Configuration.DisableSharing() }
            if ($null -ne $oldPublic) { $oldPublic.Configuration.EnableSharing(0) }
            if ($privateWasEnabled) { $Plan.Private.Configuration.EnableSharing(1) }
        } catch { Write-Warning 'Windows could not restore the previous sharing configuration.' }
        throw
    }
}

function Write-SharingStatus([string]$Message) {
    if ($Message -eq $script:LastMessage) { return }
    $script:LastMessage = $Message
    Write-Host $Message
    # No network passwords or packet contents are logged.
    if (Test-Path -LiteralPath $script:InstallRoot) {
        $log = Join-Path $script:InstallRoot 'status.log'
        if ((Test-Path -LiteralPath $log) -and (Get-Item -LiteralPath $log).Length -gt 65536) {
            Set-Content -LiteralPath $log -Value '' -Encoding UTF8
        }
        Add-Content -LiteralPath $log -Value ((Get-Date -Format s) + ' ' + $Message) -Encoding UTF8
    }
}

function Restart-SharingTask {
    $task = Get-ScheduledTask -TaskName $script:TaskName
    if ($task.State -eq 'Running' -or $task.State -eq 'Queued') {
        Stop-ScheduledTask -TaskName $script:TaskName
        for ($attempt = 0; $attempt -lt 50; ++$attempt) {
            $task = Get-ScheduledTask -TaskName $script:TaskName
            if ($task.State -ne 'Running' -and $task.State -ne 'Queued') { break }
            Start-Sleep -Milliseconds 200
        }
        if ($task.State -eq 'Running' -or $task.State -eq 'Queued') {
            throw 'The previous sharing helper could not stop. The updated helper was not started.'
        }
    }
    Start-ScheduledTask -TaskName $script:TaskName
}

function Install-Companion([string]$Source) {
    if (-not (Test-Administrator)) {
        if (-not $Source -or $Source -notlike '*.cmd') {
            throw 'Run this script with -Install in an administrator PowerShell, or use FleaByte-Sharing-Setup.cmd.'
        }
        Write-Host 'Approve the Windows prompt once to install FleaByte Internet Sharing.'
        $process = Start-Process -FilePath $env:ComSpec -Verb RunAs -WindowStyle Hidden `
            -ArgumentList @('/d', '/c', ('""' + $Source + '" /no-pause"')) -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw 'FleaByte installation did not finish.' }
        Write-Host 'Installed. Start Wi-Fi Hotspot on FleaByte, then join its Wi-Fi.'
        return
    }
    if (-not $Source) { $Source = $PSCommandPath }
    if (-not $Source -or -not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw 'Setup source file is missing.' }
    if (Test-Path -LiteralPath $script:InstallRoot) {
        if ((Get-Item -LiteralPath $script:InstallRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'The installation folder must not be a link.'
        }
    } else { New-Item -ItemType Directory -Path $script:InstallRoot | Out-Null }
    # An elevated task must execute code that ordinary users cannot modify.
    $acl = New-Object Security.AccessControl.DirectorySecurity
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($sid in @('S-1-5-18', 'S-1-5-32-544', 'S-1-5-32-545')) {
        $rights = if ($sid -eq 'S-1-5-32-545') { 'ReadAndExecute' } else { 'FullControl' }
        $rule = New-Object Security.AccessControl.FileSystemAccessRule(
            (New-Object Security.Principal.SecurityIdentifier($sid)), $rights,
            'ContainerInherit,ObjectInherit', 'None', 'Allow')
        $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $script:InstallRoot -AclObject $acl
    $content = [IO.File]::ReadAllText($Source)
    if ($Source -like '*.cmd') {
        $marker = '# FLEABYTE_' + 'POWERSHELL'
        $offset = $content.IndexOf($marker)
        if ($offset -lt 0) { throw 'The setup package is incomplete.' }
        $content = $content.Substring($offset)
    }
    Set-Content -LiteralPath $script:InstalledScript -Value $content -Encoding UTF8
    $powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $action = New-ScheduledTaskAction -Execute $powershell `
        -Argument ('-NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "' + $script:InstalledScript + '" -Watch')
    $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
    $trigger = New-ScheduledTaskTrigger -AtStartup
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
        -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
    Register-ScheduledTask -TaskName $script:TaskName -Action $action -Principal $principal `
        -Trigger $trigger -Settings $settings -Description 'Share the PC internet connection with a FleaByte USB Wi-Fi hotspot.' -Force | Out-Null
    Restart-SharingTask
    Write-Host 'Installed. Start Wi-Fi Hotspot on FleaByte, then join its Wi-Fi.'
    Write-Host ('For status: ' + (Join-Path $script:InstallRoot 'status.log'))
    Write-Host ('To remove: powershell -File "' + $script:InstalledScript + '" -Uninstall (as administrator)')
}

function Uninstall-Companion {
    if (-not (Test-Administrator)) { throw 'Administrator approval is required to uninstall the sharing companion.' }
    $task = Get-ScheduledTask -TaskName $script:TaskName -ErrorAction SilentlyContinue
    if ($null -ne $task) {
        Stop-ScheduledTask -TaskName $script:TaskName -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $script:TaskName -Confirm:$false
    }
    $dongles = @(Get-NetAdapter -IncludeHidden | Where-Object { Test-FleaByteAdapter $_ })
    $manager = New-Object -ComObject HNetCfg.HNetShare
    $connections = @(Get-SharingConnections $manager)
    $ours = @($connections | Where-Object {
        $guid = $_.Guid
        $_.Enabled -and $_.Type -eq 1 -and @($dongles | Where-Object { ([guid]$_.InterfaceGuid).ToString() -eq $guid }).Count
    })
    if ($ours.Count) {
        foreach ($connection in $connections | Where-Object { $_.Enabled -and $_.Type -eq 0 }) { $connection.Configuration.DisableSharing() }
        foreach ($connection in $ours) { $connection.Configuration.DisableSharing() }
    }
    # Delete only our known files, and never traverse a linked directory.
    if ((Test-Path -LiteralPath $script:InstallRoot) -and
        -not ((Get-Item -LiteralPath $script:InstallRoot).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        foreach ($name in @('FleaByte-Sharing.ps1', 'status.log')) {
            $file = Join-Path $script:InstallRoot $name
            if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force }
        }
        if (-not @(Get-ChildItem -LiteralPath $script:InstallRoot -Force).Count) { Remove-Item -LiteralPath $script:InstallRoot }
    }
    Write-Host 'FleaByte Internet Sharing removed.'
}

# Dot-sourcing exposes functions for read-only regression checks.
if ($MyInvocation.InvocationName -ne '.') {
    try {
        if ($Install) { Install-Companion $SourcePath }
        elseif ($Uninstall) { Uninstall-Companion }
        elseif ($Check) {
            $plan = Get-SharingPlan
            if ($plan.Ready -and -not ($plan.Private.Enabled -and $plan.Private.Type -eq 1 -and
                                      $plan.Public.Enabled -and $plan.Public.Type -eq 0)) {
                Write-Host ('Ready to share ' + $plan.Public.Name + '. Windows sharing is not enabled for this connection.')
            } else { $plan.Message | Write-Host }
        }
        elseif ($Watch) {
            if (-not (Test-Administrator)) { throw 'The companion needs its approved administrator installation.' }
            while ($true) {
                try {
                    $plan = Get-SharingPlan
                    if ($plan.Ready) { Enable-FleaByteSharing $plan }
                    Write-SharingStatus $plan.Message
                } catch { Write-SharingStatus ('Sharing paused: ' + $_.Exception.Message) }
                Start-Sleep -Seconds 5
            }
        } else { Write-Host 'Use -Install, -Check (read only), or -Uninstall. See tools/windows/README.md.' }
    } catch { Write-Error $_; exit 1 }
}
