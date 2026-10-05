# Fleabyte

Modern firmware for the LilyGO T-Dongle S3. Web UI, 12 layouts, USB storage,
no cloud. Fork of [FleaByte](https://github.com/b3rt1ng/FleaByte), adding device
tools, USB-to-Wi-Fi sharing, insertion locking, and LED settings.

**0.5.0-beta.1 — Hotspot Tool beta.** USB internet sharing, Wi-Fi client browsing
and DNS replies have been verified on one T-Dongle S3 and Windows PC. Sustained
throughput, gaming latency and broader Windows compatibility remain unmeasured.

![Fleabyte status screen](screens/screen-idle.png)

Plug it in, then unlock it with three short button presses followed by two
long presses (`SSSLL`; long means at least 200 ms by default). Until unlocked,
the dongle stays dark with Wi-Fi off and exposes only mass storage. Startup standby
can be disabled in settings; armed boot runs still stay locked and dark. Scan the code on
its screen to join, open the page, pick the keyboard layout of the machine
it is plugged into, and run a script. Payloads and settings stay on the device;
the editor needs no installation.

## What it does

* Keystroke scripting with a DuckyScript subset, from a web editor
* Twelve keyboard layouts, switchable at runtime and mid-script
* Payload library stored on the dongle, editable from the browser
* Built-in tools in the same library, with Start/Stop and optional startup after unlock
* Beta USB-to-Wi-Fi hotspot: share the PC's internet with Wi-Fi devices
* Wi-Fi join code on the screen, so a phone connects without typing a key
* Cancellable countdown before a payload starts
* Host detection, so a payload can wait for the machine instead of guessing
* microSD exposed to the host as a removable drive, with a file browser
* One-shot arming that fires the script you are editing at the next plug-in
* Status screen with run progress, and a light that reports what it is doing

| | |
|---|---|
| ![Access](screens/screen-access.png) | ![Running](screens/screen-running.png) |
| Scan to join, or read the credentials | Running a payload |

## Scope of use

Machines you own, or for which you hold written authorisation. This is a
keystroke injection tool: it types into whatever it is plugged into.

Execution is triggered from the web interface, or by arming a single run for
the next plug-in from the bar under the editor. That arming is one shot and
clears itself as it fires. An armed run enables the keyboard while the screen
and Wi-Fi stay off; a pending hard lock blocks it. Nothing else runs on plug-in, and the
button on the dongle never starts a payload.

## Hardware

A LilyGO T-Dongle S3 and nothing else. A microSD card is optional, and only
used by the USB drive feature.

* [LilyGO store](https://lilygo.cc/products/t-dongle-s3)
* [Amazon](https://www.amazon.fr/dp/B0BK9162QY)
* [Alibaba](https://www.alibaba.com/pla/LILYGO-T-Dongle-S3-ESP32-S3-Development-Board-096_1601590830049.html)

It has to be the **S3**. LilyGO also sells a T-Dongle C5, and the ESP32-C5
has no USB OTG controller: it can only present a serial port, never a
keyboard. The same goes for the C3, C6 and H2. Among the parts LilyGO uses,
only the S2, S3 and P4 can do this at all.

## Installing

**[Flash it from your browser](https://b3rt1ng.github.io/FleaByte/)**, in
Chrome or Edge on a desktop. Nothing to install: hold the button while
plugging the dongle in, click Install, pick the serial port.

Or take the image from a [release](../../releases) and write it yourself:

```sh
esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0x0 firmware.bin
```

Either way the image stops before the filesystem partition, so an update
keeps saved payloads and settings.

## Documentation

* [DOCS.md](DOCS.md) covers building from source, the script commands, the
  settings and the USB drive
* [NOTES.md](NOTES.md) collects the hardware quirks worth knowing before
  changing anything
* [Windows sharing setup](tools/windows/README.md) explains the optional hotspot
  companion and manual alternative. Windows setup requires administrator approval;
  performance and broader Windows compatibility still need validation.
* [DEVICE_TOOLS.md](DEVICE_TOOLS.md) describes the compiled-in plugin interface

## Roadmap

* Default payloads live as files in [`fleabyte/payloads/`](fleabyte/payloads) and
  are embedded at build time, not written as C constants.
* **TODO:** a tool to load payloads from the SD card (possible future feature).

## Credits

Forked from [b3rt1ng/FleaByte](https://github.com/b3rt1ng/FleaByte) (MIT).

Pin assignments and panel initialisation values come from
[LilyGO's T-Dongle S3 examples](https://github.com/Xinyuan-LilyGO/T-Dongle-S3)
(MIT). The display is driven through
[Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) and
[Adafruit ST7735](https://github.com/adafruit/Adafruit-ST7735-Library)
(BSD). Keyboard layout tables and the QR encoder ship with the
[ESP32 Arduino core](https://github.com/espressif/arduino-esp32).

## License

MIT, see [LICENSE](LICENSE).
