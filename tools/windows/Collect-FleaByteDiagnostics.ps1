#requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$Watch, [switch]$WaitForNew,
    [int]$TimeoutSeconds = 1800,
    [string]$OutputDirectory = '',
    [string]$DriveRoot
)
$ErrorActionPreference = 'Stop'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot '../../.cache/diagnostics' }
function Find-DiagnosticLog([string[]]$Roots) {
    foreach ($root in $Roots) {
        $path = Join-Path $root 'FLEABYTE-DIAGNOSTICS/hotspot.log'
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        $item = Get-Item -LiteralPath $path
        if ($item.Length -gt 525000 -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { continue }
        $reader = [IO.File]::OpenText($path)
        try { $first = $reader.ReadLine() } finally { $reader.Dispose() }
        if ($first -eq 'FLEABYTE_DIAGNOSTICS_V1') { $path }
    }
}
function Save-WindowsDiagnostics([string]$Destination) {
    $report = Join-Path $Destination 'windows.txt'
    ('Captured: ' + (Get-Date -Format o)) | Out-File -LiteralPath $report -Encoding UTF8
    foreach ($check in @(
        { Get-Service SharedAccess | Format-List Status,StartType,Name },
        { Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like 'USB\VID_303A&PID_1001*' -or $_.FriendlyName -like 'Unknown USB Device*' } | Format-List Status,FriendlyName,InstanceId },
        { Get-NetAdapter -IncludeHidden | Format-Table Name,Status,InterfaceDescription,ifIndex },
        { Get-NetAdapterBinding -AllBindings | Where-Object { $_.Name -in @(Get-NetAdapter -IncludeHidden | Where-Object InterfaceDescription -match 'RNDIS' | Select-Object -ExpandProperty Name) } | Format-Table Name,DisplayName,ComponentID,Enabled },
        { Get-NetAdapter -IncludeHidden | Where-Object InterfaceDescription -match 'RNDIS' | Get-NetAdapterStatistics | Format-List * },
        { Get-NetUDPEndpoint | Where-Object LocalPort -in @(53,67) | Format-Table LocalAddress,LocalPort,OwningProcess },
        { Get-NetIPAddress -AddressFamily IPv4 | Format-Table InterfaceAlias,IPAddress,PrefixLength },
        { Get-DnsClientServerAddress -AddressFamily IPv4 | Format-Table InterfaceAlias,ServerAddresses },
        { Get-ScheduledTask -TaskName 'FleaByte Internet Sharing' | Format-List TaskName,State },
        { Get-Content (Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'FleaByte-Sharing/status.log') -Tail 80 }
    )) {
        try { & $check | Out-String -Width 220 | Out-File -LiteralPath $report -Append -Encoding UTF8 }
        catch { ('Diagnostic check unavailable: ' + $_.Exception.Message) | Out-File -LiteralPath $report -Append -Encoding UTF8 }
    }
}
function Collect-DiagnosticLog([string]$Path, [string]$Output) {
    $folder = Join-Path $Output ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
    Copy-Item -LiteralPath $Path -Destination (Join-Path $folder 'hotspot.log')
    $previous = Join-Path (Split-Path $Path -Parent) 'hotspot.previous.log'
    if (Test-Path -LiteralPath $previous -PathType Leaf) {
        $item = Get-Item -LiteralPath $previous
        if ($item.Length -le 525000 -and -not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            Copy-Item -LiteralPath $previous -Destination (Join-Path $folder 'hotspot.previous.log')
        }
    }
    $panic = Join-Path (Split-Path $Path -Parent) 'panic.bin'
    if (Test-Path -LiteralPath $panic -PathType Leaf) {
        $item = Get-Item -LiteralPath $panic
        if ($item.Length -ge 32 -and $item.Length -le 262144 -and -not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            $stream = [IO.File]::OpenRead($panic)
            try {
                $header = New-Object byte[] 28
                $count = $stream.Read($header, 0, $header.Length)
            } finally { $stream.Dispose() }
            # Current ESP-IDF raw dump: 24-byte header, ELF body, checksum.
            if ($count -eq 28 -and [BitConverter]::ToUInt32($header, 0) -eq $item.Length -and
                $header[24] -eq 127 -and $header[25] -eq 69 -and $header[26] -eq 76 -and $header[27] -eq 70) {
                Copy-Item -LiteralPath $panic -Destination (Join-Path $folder 'panic.bin')
                Write-Host 'Copied saved firmware crash dump: panic.bin'
            }
        }
    }
    Save-WindowsDiagnostics $folder
    Write-Host ('Collected FleaByte diagnostics: ' + [IO.Path]::GetFullPath($folder))
    Get-Content -LiteralPath (Join-Path $folder 'hotspot.log') -Tail 45 | Write-Host
    return $folder
}
if ($MyInvocation.InvocationName -ne '.') {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $baseline = @{}
    $firstPass = $true
    $trace = $null; $nextSnapshot = Get-Date
    if ($Watch) {
        New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
        $trace = Join-Path $OutputDirectory ('windows-watch-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.jsonl')
    }
    Write-Host 'Waiting for FleaByte SD card. Reproduce the problem, then stop Wi-Fi Hotspot to mount the card.'
    do {
        if ($trace -and (Get-Date) -ge $nextSnapshot) {
            $nextSnapshot = (Get-Date).AddSeconds(5)
            $snapshot = [ordered]@{ time=(Get-Date -Format o) }
            try { $snapshot.service = [string](Get-Service SharedAccess).Status } catch { $snapshot.serviceError=$_.Exception.Message }
            try { $snapshot.usb = @(Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like 'USB\VID_303A&PID_1001*' -or $_.FriendlyName -like 'Unknown USB Device*' } | Select-Object Status,FriendlyName,InstanceId) } catch { $snapshot.usbError=$_.Exception.Message }
            $snapshot.network = @([Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces() | Where-Object Description -match 'RNDIS' | Select-Object Name,Description,OperationalStatus)
            ($snapshot | ConvertTo-Json -Depth 5 -Compress) | Add-Content -LiteralPath $trace -Encoding UTF8
        }
        $roots = if ($DriveRoot) { @($DriveRoot) } else {
            @([IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady -and $_.DriveType -in @('Removable','Fixed') } | ForEach-Object { $_.RootDirectory.FullName })
        }
        foreach ($root in $roots) {
            try {
                foreach ($path in @(Find-DiagnosticLog @($root))) {
                    # FAT timestamps depend on the dongle's clock. Compare content,
                    # so logs written without internet/time sync are still collected.
                    $fingerprint = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
                    if ($WaitForNew -and $firstPass) { $baseline[$path] = $fingerprint; continue }
                    if ($WaitForNew -and $baseline.ContainsKey($path) -and $baseline[$path] -eq $fingerprint) { continue }
                    $folder = Collect-DiagnosticLog $path $OutputDirectory
                    if ($trace) { Copy-Item -LiteralPath $trace -Destination (Join-Path $folder 'windows-watch.jsonl') }
                    exit 0
                }
            } catch { Write-Host ('Waiting for the card to finish mounting: ' + $_.Exception.Message) }
        }
        $firstPass = $false
        if (-not $Watch) { throw 'No mounted FleaByte diagnostic log found. Stop Wi-Fi Hotspot first, or run with -Watch.' }
        Start-Sleep -Seconds 2
    } while ((Get-Date) -lt $deadline)
    throw 'Timed out waiting for the diagnostic SD card.'
}
