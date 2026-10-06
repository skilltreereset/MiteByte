$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$script = Join-Path $root 'tools/windows/MiteByte-Sharing.ps1'
$tokens = $null; $errors = $null
[Management.Automation.Language.Parser]::ParseFile($script, [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count) { throw ($errors.Message -join '; ') }
# Syntax-check the command typed by the generated script without executing it.
$scriptHeader = Get-Content (Join-Path $root 'mitebyte/generated/windows_setup_script.h') -Raw
$bootstrapLine = ($scriptHeader -split '\r?\n' | Where-Object { $_ -like 'STRINGLN $ErrorActionPreference*' })
if (@($bootstrapLine).Count -ne 1) { throw 'The keyboard setup bootstrap is missing.' }
$bootstrap = $bootstrapLine.Substring('STRINGLN '.Length)
[Management.Automation.Language.Parser]::ParseInput($bootstrap, [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count) { throw ($errors.Message -join '; ') }
. $script
. (Join-Path $root 'tools/windows/Repair-MiteByteSharing.ps1')

function Assert($condition, $message) { if (-not $condition) { throw $message } }
$encoded = [regex]::Match($bootstrap, "FromBase64String\('([^']+)'\)").Groups[1].Value
$bytes = [Convert]::FromBase64String($encoded)
$memory = [IO.MemoryStream]::new([byte[]]$bytes,$false)
$zip = [IO.Compression.GZipStream]::new($memory,[IO.Compression.CompressionMode]::Decompress)
$output = [IO.MemoryStream]::new()
$zip.CopyTo($output)
$decoded = $output.ToArray()
$zip.Dispose(); $memory.Dispose(); $output.Dispose()
$expected = [IO.File]::ReadAllBytes((Join-Path $root '.cache/generated/MiteByte-Sharing-Setup.cmd'))
Assert ([Convert]::ToBase64String($decoded) -eq [Convert]::ToBase64String($expected)) 'Windows PowerShell must unpack the exact reviewed installer, without running it'
$script:mockProduct = 'MiteByte USB Hotspot'
$script:mockSerial = 'ABCDEF-H'
function Get-PnpDeviceProperty {
    param($InstanceId, $KeyName, $ErrorAction)
    if ($KeyName -eq 'DEVPKEY_Device_Parent' -and $InstanceId -like '*MI_00*') {
        return [pscustomobject]@{Data=('USB\VID_303A&PID_0002\' + $script:mockSerial)}
    }
    if ($KeyName -eq 'DEVPKEY_Device_BusReportedDeviceDesc' -and $InstanceId -notlike '*MI_00*') {
        return [pscustomobject]@{Data=$script:mockProduct}
    }
    return $null
}
$candidate = [pscustomobject]@{PnPDeviceID='USB\VID_303A&PID_0002&MI_00\network-interface'}
Assert (Test-MiteByteAdapter $candidate) 'Identify the hotspot through its composite USB parent'
$script:mockSerial = 'ABCDEF-S'
Assert (-not (Test-MiteByteAdapter $candidate)) 'Do not match storage mode'
$script:mockSerial = 'ABCDEF-H'; $script:mockProduct = 'Another USB network adapter'
Assert (-not (Test-MiteByteAdapter $candidate)) 'Do not configure an unrelated USB network adapter'
$candidate.PnPDeviceID = 'PCI\network-interface'
Assert (-not (Test-MiteByteAdapter $candidate)) 'Do not configure a PCI adapter as the dongle'
$adapters = @(
    [pscustomobject]@{InterfaceIndex=1; Name='Wi-Fi'; Status='Up'},
    [pscustomobject]@{InterfaceIndex=2; Name='Ethernet'; Status='Up'},
    [pscustomobject]@{InterfaceIndex=3; Name='MiteByte'; Status='Up'},
    [pscustomobject]@{InterfaceIndex=4; Name='Disconnected'; Status='Disconnected'}
)
$interfaces = @(
    [pscustomobject]@{InterfaceIndex=1; InterfaceMetric=20},
    [pscustomobject]@{InterfaceIndex=2; InterfaceMetric=5},
    [pscustomobject]@{InterfaceIndex=3; InterfaceMetric=1},
    [pscustomobject]@{InterfaceIndex=4; InterfaceMetric=0}
)
$routes = @(1,2,3,4 | ForEach-Object { [pscustomobject]@{InterfaceIndex=$_; RouteMetric=1; DestinationPrefix='0.0.0.0/0'} })
Assert ((Select-Upstream $routes $interfaces $adapters 3).Name -eq 'Ethernet') 'Select the live adapter by total route metric, excluding the dongle and disconnected routes'
$adapters[1].Status = 'Disconnected'
Assert ((Select-Upstream $routes $interfaces $adapters 3).Name -eq 'Wi-Fi') 'Follow an Ethernet-to-Wi-Fi change'
$adapters[0].Status = 'Disconnected'
Assert ($null -eq (Select-Upstream $routes $interfaces $adapters 3)) 'Do not share the dongle back into itself when the PC has no upstream'
$connections = @([pscustomobject]@{Enabled=$true; Type=1; Guid='another-adapter'})
Assert (Test-SharingConflict $connections 'dongle') 'Detect existing sharing with another adapter'
$connections[0].Guid = 'dongle'
Assert (-not (Test-SharingConflict $connections 'dongle')) 'Allow restoring our own sharing connection'

# Exercise the actual configuration function against mocks, without touching
# Windows network settings, administrator rights, files or scheduled tasks.
$script:operations = [Collections.Generic.List[string]]::new()
$script:serviceStatus = 'Running'
$script:serviceStarts = 0
$script:serviceFails = $false
function Get-Service {
    param($Name)
    Assert ($Name -eq 'SharedAccess') 'Inspect only the ICS service'
    $value = [pscustomobject]@{Status=$script:serviceStatus}
    $value | Add-Member ScriptMethod WaitForStatus {
        param($status, $timeout)
        Assert ($script:serviceStatus -eq 'Running') 'Wait for the service to start'
    }
    return $value
}
function Start-Service {
    param($Name)
    Assert ($Name -eq 'SharedAccess') 'Start only the ICS service'
    ++$script:serviceStarts
    if ($script:serviceFails) { throw 'Simulated service startup failure' }
    $script:serviceStatus = 'Running'
}
function Mock-Configuration([string]$name, [bool]$enabled, [bool]$fail = $false) {
    $value = [pscustomobject]@{Name=$name; SharingEnabled=$enabled; Fail=$fail}
    $value | Add-Member ScriptMethod EnableSharing {
        param($type)
        $script:operations.Add($this.Name + ':enable:' + $type)
        if ($this.Fail) { throw 'Simulated Windows rejection' }
        $this.SharingEnabled = $true
    }
    $value | Add-Member ScriptMethod DisableSharing {
        $script:operations.Add($this.Name + ':disable'); $this.SharingEnabled = $false
    }
    return $value
}
$private = [pscustomobject]@{Enabled=$true; Type=1; Configuration=(Mock-Configuration 'dongle' $true)}
$public = [pscustomobject]@{Enabled=$true; Type=0; Configuration=(Mock-Configuration 'source' $true)}
$plan = [pscustomobject]@{Private=$private; Public=$public; Connections=@($private,$public)}
Enable-MiteByteSharing $plan
Assert ($script:operations.Count -eq 0) 'An already shared connection must not trigger repeated configuration calls'
$script:serviceStatus = 'Stopped'
Enable-MiteByteSharing $plan
Assert ($script:serviceStarts -eq 1 -and $script:operations.Count -eq 0) 'Recover a stopped ICS service without reconfiguring shared adapters'
Enable-MiteByteSharing $plan
Assert ($script:serviceStarts -eq 1) 'Leave the running ICS service alone'
$script:serviceStatus = 'Stopped'; $script:serviceFails = $true
$serviceFailed = $false
try { Enable-MiteByteSharing $plan } catch { $serviceFailed = $true }
Assert $serviceFailed 'Do not report sharing success when its service cannot start'
$script:serviceFails = $false; $script:serviceStatus = 'Running'
$plan | Add-Member NoteProperty Ready $true
$script:operations.Clear()
Repair-SharingConnection $plan
Assert (($script:operations -join ',') -eq 'dongle:disable,source:disable,source:enable:0,dongle:enable:1') 'Explicit repair must refresh only the validated sharing pair'
$script:operations.Clear()
$plan.Ready = $false; $refused = $false
try { Repair-SharingConnection $plan } catch { $refused = $true }
Assert ($refused -and $script:operations.Count -eq 0) 'Repair must refuse a missing dongle or another sharing destination'
$plan.Ready = $true
$public.Enabled = $false
$public.Configuration.SharingEnabled = $false
Enable-MiteByteSharing $plan
Assert (($script:operations -join ',') -eq 'source:enable:0') 'Switch only the public source when the dongle is already private'
$script:operations.Clear()
$private.Enabled = $false; $private.Configuration.SharingEnabled = $false; $private.Configuration.Fail = $true
$old = [pscustomobject]@{Enabled=$true; Type=0; Configuration=(Mock-Configuration 'previous' $true)}
$plan.Connections = @($old)
$failed = $false
try { Enable-MiteByteSharing $plan } catch { $failed = $true }
Assert $failed 'Report a rejected setup'
Assert ($script:operations.Contains('source:disable') -and $script:operations.Contains('previous:enable:0')) 'Restore the original public sharing connection on failure'
$script:taskState = 'Running'; $script:taskStopFails = $false
$script:taskCalls = [Collections.Generic.List[string]]::new()
function Get-ScheduledTask { param($TaskName); return [pscustomobject]@{State=$script:taskState} }
function Stop-ScheduledTask { param($TaskName); $script:taskCalls.Add('stop'); if (-not $script:taskStopFails) { $script:taskState = 'Ready' } }
function Start-ScheduledTask { param($TaskName); $script:taskCalls.Add('start') }
function Start-Sleep { param($Milliseconds) }
Restart-SharingTask
Assert (($script:taskCalls -join ',') -eq 'stop,start') 'An update must restart the existing helper to load the new script'
$script:taskCalls.Clear(); Restart-SharingTask
Assert (($script:taskCalls -join ',') -eq 'start') 'First installation starts a ready task directly'
$script:taskState = 'Running'; $script:taskStopFails = $true; $script:taskCalls.Clear()
$restartFailed = $false
try { Restart-SharingTask } catch { $restartFailed = $true }
Assert ($restartFailed -and -not $script:taskCalls.Contains('start')) 'Do not claim an update succeeded while the old helper is still running'
Write-Host 'PASS: helper syntax, connection switching, adapter exclusion, conflicts, idempotence and rollback (no system changes)'
