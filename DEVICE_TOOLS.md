# Built-in device tools

Tools are firmware modules with a shared lifecycle. They appear beside scripts
in the Library but do not run through the macro script interpreter. Adding a tool
requires rebuilding firmware; uploading executable plugins at runtime is not
supported.

## Adding a tool

1. Put the tool's implementation files in their own folder under
   `mitebyte/src/tools/<tool>/` (one folder per plug-and-play tool). Arduino
   compiles the sketch's `src/` directory recursively. Shared infrastructure
   (e.g. SD diagnostic logging) lives outside `src/tools/`, under `src/diagnostics/`.
2. Export a `const ToolPlugin` from a small header. The interface is in
   `mitebyte/tools.h`: ID, title, description, begin/start/stop/tick/status,
   notice, optional setup download metadata and an optional library setup script name.
3. Include that header in `mitebyte/tools.cpp` and add its address to `s_plugins`.
4. Return compact status text and label/value details. The HTTP API and web UI
   consume that metadata without branches for individual tools.

`begin` runs once after USB startup. `start` returns an error and cleans up any
partial initialization if it cannot start. `stop` releases resources and
restores changed device services. `tick` must return promptly so the button,
HTTP server and lock continue working. `status` should not perform network IO.
Callbacks run on the application loop; hardware/USB callbacks need their own
thread-safe handoff.

The registry allows one active tool. Starting while insertion/hard locked is
rejected. Hard lock stops the tool before shutting down Wi-Fi. Screen lock
leaves it running. A saved startup tool waits until the device goes online and
any armed/running script finishes. Stopping cancels a pending startup for that
online session. The startup preference is stored in LittleFS, outside the
script directory, and survives firmware updates. Resetting settings clears it.

## USB ownership

An exclusive tool can provide a complete `UsbToolProfile` to
`usbModeSetToolProfile()`. It contains configuration/device descriptors, product
text and a stable serial suffix. Switching tears down and reinitializes the USB
controller in the USB task, then reconnects with the new descriptors. Tools must
quiesce their packet producers before switching; the call reports restart errors.
Stopping restores the normal active profile; entering standby restores the
storage-only profile. Keyboard readiness is false while a tool owns USB.

Do not switch profiles during host drive IO. The hotspot checks that script
execution is idle before starting, and `/api/run` rejects runs while a tool is
active. USB callbacks use copied, bounded queues; forwarding runs through an
ESP-IDF Ethernet netif and the existing AP's IPv4 NAPT.

## Wi-Fi Hotspot

The data path is PC internet → Windows ICS → USB RNDIS → dongle IPv4 NAT →
Wi-Fi clients. Wi-Fi credentials and the management address stay the same.
DHCP on USB gets the address, gateway and DNS from Windows. Overlapping USB/AP
subnets produce an error instead of enabling forwarding.

Starting stops captive wildcard DNS and binds a DNS proxy to the AP address.
UDP requests have translated IDs and matched resolver replies, eight outstanding
slots and a three-second timeout. EDNS advertises at most 1232 bytes. UDP packets
over 4096 bytes are discarded rather than silently truncated. Two nonblocking
TCP relay slots support large DNS replies, partial writes, backpressure and a
ten-second idle timeout. The firmware does not log packet contents or passwords.

Receive and DNS servicing run in a dedicated task. USB OUT uses backpressure
when its bounded queue fills. USB IN is kicked when a frame is queued and again
on completion, retaining a frame if submission fails. Sent counters represent
completed transfers. Start/stop synchronizes task access and tags frames by run
generation so an old run cannot feed packets into the next one.

RNDIS IN submits one endpoint-sized USB packet at a time, retaining the complete
message buffer until its final short packet completes. The wire message and
Ethernet completion counters remain unchanged. A rejected continuation retries
the same offset. This beta workaround avoids the multi-packet controller
request observed to stall on hardware. Browsing and USB completions resumed on
the development device; sustained throughput and reliability still need testing.

RNDIS is a private application driver beside Arduino's built-in USB classes,
using distinct symbols. No installed SDK files are patched. Its upstream source,
license and local adaptations are in `mitebyte/src/usb_ethernet/UPSTREAM.md`.
The build uses Arduino ESP32 3.3.12 (TinyUSB 0.21 driver interface); older cores
are not validated.

## API

`GET /api/state` includes `tools`, `activeTool` and `startupTool`, preserving the
existing `scripts` array. Tool actions accept URL-encoded `id`:

| Endpoint | Effect |
|---|---|
| `POST /api/tools/start` | Start the identified tool |
| `POST /api/tools/stop` | Stop the active tool if its ID matches; empty ID stops any tool |
| `POST /api/tools/startup` | Save startup ID; empty ID disables startup |
| `GET /api/tools/setup?id=…` | Download that tool's optional setup file |

The Windows package is generated from the reviewed PowerShell source by
`python tools/build_helpers.py`. Build/release scripts run the generator. The
generated firmware headers are included in the repository for Arduino IDE builds;
regenerate them whenever the helper changes. The generator also bundles the same
installer into `13-windows-hotspot-setup.txt`, a one-time keyboard script. This
uses the existing interpreter, leaves administrator approval to the user, and
does not run automatically when starting a tool. Initial seeding adds it to
the library without overwriting edited scripts. The Windows helper is optional and
separate from device startup. See `tools/windows/README.md`.

### Wi-Fi address assignment diagnostics

The hotspot checks the AP DHCP server before starting. It starts a stopped
server and refuses to report a running tool if that server cannot start.
The SD trace includes `AP_STATE` (AP address, DHCP state and interface up state)
and standard Arduino Wi-Fi events: `WIFI_CLIENT_CONNECTED`,
`WIFI_CLIENT_ADDRESS` after DHCP assignment, and `WIFI_DISCONNECT`.
No Wi-Fi packet callbacks are replaced. An experimental packet observer was
removed after a hardware crash report; its native tests did not validate
real Wi-Fi task behavior.

USB reception uses four fixed payload buffers. Each credit stays owned until
the Ethernet custom pbuf invokes the driver's free callback, including error
paths, following [Espressif's Ethernet input ownership](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_netif/lwip/netif/ethernetif.c).
Two additional queued receive frames and one held USB frame provide
bounded backpressure; the host retries while USB OUT is unarmed. Reception
pauses when internal heap is below 12 KiB or its largest free block is below
2 KiB. The six-frame transmit queue also bounds memory; no receive payload
is allocated separately for each packet. Existing lwIP-owned buffers are not
reset or reused when the USB profile changes.

`FLOW` records owned receive buffers, pauses, oldest active IN transfer age
and largest free block. `USB_BULK` records software endpoint state, active
transfer length and rejected OUT renewals. `USB_HW` reads ESP32-S3 endpoint
control, remaining transfer size, interrupt and FIFO state in the USB task;
it does not alter controller registers or abort stalled transfers. An OUT
submission rejected by TinyUSB is retained for retry. These records can
distinguish application backpressure from a transfer that stops completing.
They do not establish the cause of a missing hardware completion by themselves.
`USB_IN_PROGRESS` adds message length, completed byte offset, active USB packet
length and pending continuation retry, so a stalled individual packet can be
distinguished from a rejected continuation.

The architecture follows Espressif's router pattern: separate upstream DHCP
client and downstream DHCP server interfaces, separate subnets, upstream
default route and NAPT on the downstream interface. See the
[Espressif bridge implementation](https://github.com/espressif/esp-iot-bridge/blob/master/components/iot_bridge/src/bridge_common.c).
Its USB examples commonly provide internet to a PC; they are not direct proof
of Windows ICS supplying internet to a USB device. The existing RNDIS transport
is retained for Windows 10 compatibility.

## Validation limits

When the SD card is first identified after boot, a `BOOT` record is appended
before USB mass storage can expose it. It includes the reset reason and the
installed application's ELF hash; remounts do not create extra boot records.
This keeps the next automatic reset visible even if the hotspot is never
started again. A later power reconnect adds its own record instead of replacing
the earlier reason. Records use the same bounded diagnostic files. Common
reset values are 1 (power-on), 4 (panic), 5 (interrupt watchdog), 6 (task watchdog)
and 9 (brownout). They describe the reset mechanism, not its underlying cause.

Hotspot diagnostics run automatically for each sharing session when an SD card
is present. Starting takes the card away from USB mass storage. A background
writer saves `/MITEBYTE-DIAGNOSTICS/hotspot.log`, retaining the preceding session
as `hotspot.previous.log`; each file is bounded to about 512 KiB. Stopping waits
for queued records to be flushed and closed before restoring the previous USB
drive setting. MSC sector operations and read-ahead share a gate, so revoking
media waits for in-flight host reads/writes before a remount. Host-to-firmware
handoff refreshes the mount after raw host writes; returning to the host syncs
the existing mount without tearing it down again. If card identification fails,
the watch task retains and reapplies the requested USB exposure when it recovers.
If enumeration fails, a power reconnect also exposes whatever
records were flushed; the final in-memory records can be lost on power removal.

Records include build/reset information, USB restart stages and RNDIS control
requests, packet filters, USB resets, DHCP message types and transaction IDs,
ARP and DNS protocol headers, address/gateway changes, forwarding state, queues,
transfer completions/errors, DNS statistics, and memory/stack counters every two
seconds. Text logs contain no packet bodies, DNS names, Wi-Fi keys or script contents.
The bounded nonblocking queue reports dropped diagnostic records; its writer
runs outside USB and TCP/IP callbacks. SD failure is shown in the status panel
and does not prevent the hotspot from starting.

Before exposing the SD card, boot appends the reset reason and full application
ELF hash. Reset reason 4 means a firmware panic; later power reconnects append
separate reason-1 records. A valid flash crash dump is checksum-checked and
exported as `panic.bin` in the same diagnostic directory. Unlike the text logs,
this binary contains task memory and is kept for local debugging. Export uses
512-byte chunks, publishes the file only after a complete write, and leaves
the flash original intact. The collector copies bounded, complete ELF dumps
alongside the text logs; decoding requires the matching firmware ELF.

Both build layouts reserve 256 KiB for crash dumps in the tail of the second
firmware slot. The starts of both firmware slots and the settings filesystem's
start/size are unchanged. PlatformIO uses coredump at `0xBD0000` and Arduino CLI
at `0x7D0000`; the two existing filesystem layouts remain distinct. The second
slot is 256 KiB smaller, so OTA images must fit that slot. A normal upload must
include the updated partition table; application-only OTA cannot apply this
diagnostic layout. This diagnostic change does not establish the panic's cause.

USB receive and transmit scratch frames reside in reserved storage, rather
than the receive task's stack. lwIP core locking can execute DNS socket output
and the driver's transmit callback synchronously on that same task. The old
two-frame nesting caused the captured `usb-net-rx` stack-watchpoint panic.
Transmit scratch is protected through the nonblocking queue copy, so concurrent
callers cannot overwrite it; diagnostics and USB scheduling run after release.
The receive scratch has one worker owner. Compiled stack checks bound the
driver callback and worker's own frames to 512 bytes each; that bound is not
the total SDK call-chain stack usage, which still needs hardware monitoring.

Start a read-only collector on Windows before reproducing the issue:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/windows/Collect-MiteByteDiagnostics.ps1 -Watch -WaitForNew
```

It watches for the diagnostic folder on a mounted card, copies current and
previous logs into `.cache/diagnostics/<timestamp-id>/`, and adds Windows adapter,
USB, DNS, sharing-service and helper status. While waiting, it records USB and
service state every five seconds. It exits after one collection or 30 minutes.
`-WaitForNew` ignores unchanged files already mounted when watching begins;
content hashes avoid dependence on the dongle's FAT clock. The collector changes
no Windows networking or scheduled tasks and needs no administrator access to
copy the SD logs. Unavailable Windows checks are recorded as errors in the report.

DHCP `code`: 1 Discover, 2 Offer, 3 Request, 5 ACK, 6 NAK; DNS `code` is the
response code (0 success, 2 server failure, 3 nonexistent name). Log timestamps
are milliseconds since device boot. SD flushes improve power-loss recovery but
cannot guarantee recovery from an interrupted card write.

Compilation, native lifecycle/packet checks, linked USB descriptors, mocked UI
and Windows sharing logic have automated coverage. Browser previews use simulated
API responses. Helper installation, physical USB enumeration, DHCP, NAT and Wi-Fi
client browsing were observed on one Windows PC and T-Dongle S3. A retained panic
was exported and decoded against its matching ELF; stopping the hotspot restored
SD access for automatic log collection. Broader Windows compatibility, sustained
network load and gaming benchmarks remain pending. IPv6 forwarding, automatic
port mapping and guaranteed VPN compatibility are not implemented.
