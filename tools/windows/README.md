# MiteByte Internet Sharing

Optional Windows 10/11 companion for **Wi-Fi Hotspot** in the device Library.
It shares the PC's active IPv4 connection over USB; clients join the dongle's
Wi-Fi. The PC must already have working internet, through Wi-Fi or Ethernet.
This does not connect the PC to the internet through the dongle.

The firmware and helper are beta. Helper installation, USB enumeration, address
assignment and browsing from a Wi-Fi client have been observed on one Windows
PC and T-Dongle S3. Sustained throughput, gaming latency and compatibility across
Windows 10/11 installations still need testing.

## One-time installation

1. Unlock the dongle and open its web UI. Stop the hotspot if it is running.
2. Select **Wi-Fi Hotspot → Windows setup → Open setup script**
4. After the PC reports successful installation, start **Wi-Fi Hotspot** and
   join the dongle's Wi-Fi on the device needing internet.
   Open the dongle UI at its IP address, normally `http://192.168.4.1`.

Alternatively, download `MiteByte-Sharing-Setup.cmd`, transfer it to the PC if
needed, and run it directly. Both routes install the same companion and require
the same administrator approval.

The optional **Start automatically after unlock** checkbox starts the firmware
tool each time it goes online. Insertion/hard lock still applies; it does not
bypass the unlock gesture. Screen lock keeps sharing running.

Installation writes `MiteByte-Sharing.ps1` to
`C:\Program Files\MiteByte-Sharing`, protected against ordinary user writes,
and registers **MiteByte Internet Sharing** as a SYSTEM startup task. It starts
immediately and checks every five seconds. It identifies this firmware's USB
product and serial suffix, follows the PC's lowest-metric active default route,
and leaves an existing sharing destination belonging to another adapter alone.

If the dongle keeps requesting DHCP without an offer even though the sharing
service is running, leave Wi-Fi Hotspot running and run
`tools/windows/Repair-MiteByteSharing.ps1` in Windows PowerShell as administrator.
It reapplies the validated MiteByte sharing pair and attempts to restore it on
failure. It does not restart the network adapters or install a new helper.
Check the dongle's USB address and an actual website afterward; successful
configuration alone does not prove internet access.
If several MiteByte hotspots are connected, it waits for only one.

The task uses already approved system rights; it does not request elevation on
each plug-in. Windows sharing policy or consent can still block it. Microsoft's
[EnableSharing documentation](https://learn.microsoft.com/en-us/windows/win32/api/netcon/nf-netcon-inetsharingconfiguration-enablesharing)
describes an additional sharing notification; behavior under the startup task
must be checked on real Windows installations. An installation approval does
not bypass Windows or an administrator password.

No remote-command agent is installed. The helper only detects adapters, reads
routes and configures ICS. It records changes/errors in `status.log`, without
Wi-Fi passwords or traffic contents. Helper CPU/RAM use is not benchmarked.

## Manual alternative

Start the hotspot so Windows can see its USB network adapter. Run `ncpa.cpl`,
open the PC's internet adapter **Properties → Sharing**, enable Internet
Connection Sharing, and select the MiteByte USB network adapter. This also
requires administrator approval. The companion is unnecessary if you configure
sharing yourself; you may need to repeat the selection when switching upstream
adapters.

RNDIS was chosen for the Windows 10/11 path. Native Windows support is documented
in Microsoft's [USB network driver reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/overview-of-remote-ndis--rndis-).
Driver binding was observed on the development PC; other Windows installations
still need hardware checks.

## Diagnose or remove

Read-only diagnosis, from PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\Program Files\MiteByte-Sharing\MiteByte-Sharing.ps1' -Check
Get-Content 'C:\Program Files\MiteByte-Sharing\status.log' -Tail 20
```

Uninstall from an **administrator PowerShell**:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\Program Files\MiteByte-Sharing\MiteByte-Sharing.ps1' -Uninstall
```

This removes the named task and known helper files. It disables sharing only
when the private sharing destination is identified as a MiteByte dongle. If you
remove it while the dongle is disconnected, inspect Windows' Sharing tab and
disable any remaining configuration manually.

## Connection and gaming limits

The hotspot reports USB connection and address assignment, rather than claiming
that the internet or a game server is reachable. Check a website first. A VPN,
managed PC, captive portal, firewall or provider policy can prevent sharing.
Both Windows ICS and the dongle add NAT; games requiring inbound connections
may have restrictions. No automatic port forwarding is provided.

No measured throughput or ping overhead is available for this implementation.
Benchmark an idle connection and one under load before relying on it for gaming.
The ESP32-S3 uses USB Full Speed; the nominal USB link rate is not an internet
throughput estimate.
