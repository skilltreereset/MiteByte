# Regression checks

Run from the repository root:

```sh
python tests/run_native.py
python tests/run_rndis_driver.py
node tests/web_ui_test.mjs
python tests/check_usb_descriptors.py .pio/build/mitebyte/firmware.elf
python tests/helper_package_test.py
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/windows_helper_test.ps1
```

The native check needs g++ or clang++ and compiles the production lock,
validation and display sources against fake hardware IO. It checks gesture
conflicts, button holds, screen light gating, immediate LED blanking, queued UI
hard lock and persistence failures, automatic startup with standby disabled,
hard-lock precedence and reset.
The menu view check compiles the production `menu_view.cpp` against a fake panel
that keeps what was pushed to it. It checks the picture in landscape and
portrait (frame, ruler, labels, the running-tool colour, alert and hold fill),
that a rolling list settles to the same picture as a direct draw, that a
settled list repaints only the strips touching the frame (marquee, hold fill),
that nothing is pushed while nothing changes, and that the view never touches
the heap.
It also checks the production tool registry's lifecycle, autostart/lock
precedence, storage failures, malformed RNDIS messages and DNS/EDNS packet bounds.
RNDIS checks accept Windows' 1024-byte response requests while bounding incoming
commands to the device buffer.
USB receive checks fill a bounded queue, hold and retry a copied frame without
rearming USB prematurely, preserve packet order, and cancel pending traffic.
The pending flag also controls retry scheduling: immediately queued frames
must not request a deferred USB retry event.
The DNS relay check exercises production code with fake sockets: source/ID
matching, queue limits, oversized packets, TCP partial writes/backpressure,
connection failures, timeouts and cleanup, without opening real sockets.
It also reproduces the captive server retaining port 53 after `stop()`, then
checks rebinding releases it across repeated portal/hotspot handoffs while
keeping the asynchronous callback owner alive.

The JavaScript check runs the script embedded in `web_assets.h` with a mocked
DOM/API. It checks disabled screen controls, both unlock actions, LED-only requests
while locked, conflicting gestures, the startup switch, reset errors, and hard-lock recovery and errors.
It does not verify browser layout or the firmware's HTTP server.
Tool checks cover selection without losing unsaved edits, Start/Stop failures,
exclusive execution, script-busy state and failed startup saves.

The descriptor check needs pyelftools (included in the local PlatformIO Python
environment) and a built ELF. It inspects the linked descriptor bytes and verifies
that the firmware overrides the framework's weak callbacks. It is not a physical
USB enumeration test.
It includes the hotspot profile and application RNDIS driver callbacks.

The driver check uses the installed PlatformIO TinyUSB headers and native C/C++
compilers. It exercises the production C driver at every Ethernet frame size,
reassembly from endpoint-sized USB requests, final short-packet padding,
message ownership until the final completion, failed and rejected continuations,
real initialize and filter requests, and repeated reset/deinit. It also compiles the production
USB mode switch to check controller teardown, descriptor changes and reinit
ordering in USB task context across ten start/stop cycles and failure cases.
Controller and endpoint operations are mocked; this is not hardware validation.

Receive buffer checks exercise delayed pbuf ownership, bounded full-size bursts,
heap/fragmentation reserves and concurrent acquire/release. RNDIS checks also
reject an OUT renewal and verify its retry without double-arming an endpoint.

The package check verifies that the firmware download and compressed keyboard
script contain the same current PowerShell helper and launcher, stay within
size limits, and include no automatic elevation-response keystrokes. The
PowerShell check parses the actual helper and typed bootstrap, verifies that
Windows PowerShell can unpack the exact installer in memory without running it,
and exercises route selection, sharing conflicts, idempotence and rollback
against mocks. It never installs a task or changes host networking.

On hardware, check cold locked boot, gesture unlock, screen settings while
locked, hard lock during a harmless test script, reinsertion counts, and an
armed boot run with display/Wi-Fi off. USB reconnects when changing interface
sets; perform these checks without a host file transfer in progress.

For the hotspot, verify Windows 10 and 11 automatic RNDIS binding, ICS DHCP,
web access and UDP/TCP DNS from a Wi-Fi client, then stop/restart and hard lock.
Test unplug/replug, autostart, upstream Wi-Fi/Ethernet changes, no upstream,
existing ICS destination conflicts and approved helper installation/removal.
Test large DNS replies and concurrent traffic while operating the button/UI.
Measure sustained throughput, packet loss and idle/loaded game-server ping;
record the PC's direct-connection results as a baseline. USB enumeration, upstream
DHCP, DNS replies and browsing were observed on one development PC and dongle;
the wider compatibility, load and gaming checks remain pending.

`python tests/run_native.py` also runs the production asynchronous diagnostic
writer with real concurrent native threads and simulated queues/SD operations.
It checks nonblocking queue overflow, flush-before-host ownership, repeated
sessions and SD failures. Protocol metadata tests cover malformed lengths,
fragment offsets and DHCP option bounds. Run the read-only collector fixture:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/diagnostic_collector_test.ps1
```

Mounting after Stop and automatic collection were observed on the development
dongle. Physical SD rotation and power-loss recovery still need verification.

The native storage fixture compiles production `usb_drive.cpp` and holds real
concurrent simulated host reads/writes open while firmware requests SD ownership.
It checks that remount waits for those operations, late callbacks cannot touch
firmware-owned media, restoring USB storage avoids an unnecessary remount, and
a later successful card identification restores the requested USB exposure.
It also checks that the boot reset/ELF record is written before host access and
does not repeat when the same boot remounts the card.
Crash export checks cover full ELF hash logging, checksum rejection, oversize
rejection, chunked copies and withholding publication after a flash read fails.
Partition checks protect filesystem geometry and reject overlapping ranges;
collector checks reject incomplete dumps and unrelated binary files. The SDK's
actual panic writer and SD export produced a complete, decodable dump on the
development dongle; additional hardware failure cases remain untested.

Native transmit scratch tests exercise concurrent callers, queue ownership,
full queues, Ethernet frame bounds and startup allocation failure. Verify the
compiled ESP32-S3 callback/worker frames after building:

```powershell
python tests/check_hotspot_stack.py .pio/build/mitebyte/firmware.elf
```

This rejects the previous 1.5 KiB automatic frame allocations. It does not bound
all SDK/lwIP stack usage; monitor the hardware receive-stack minimum under DNS
and forwarding load, while retaining the panic exporter for any further crash.
