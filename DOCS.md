# MiteByte documentation

Everything beyond getting one running. The overview lives in the
[README](README.md), and the hardware quirks in [NOTES.md](NOTES.md).

The Hotspot Tool feature is versioned **0.5.0-beta.1**. Prerelease tags containing
a hyphen produce a prerelease download and leave the stable browser flasher
unchanged; stable version tags update it. The firmware version must match the tag
without its `v` prefix.

## Building

Uses Python 3, ESP32 Arduino core **3.3.12** and USB mode set to
**USB-OTG (TinyUSB)**. The "Hardware CDC and JTAG" mode cannot do HID, and
the sketch refuses to build if you select it.

### arduino-cli

```sh
arduino-cli config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.12
arduino-cli lib install "Adafruit GFX Library" "Adafruit ST7735 and ST7789 Library"

./tools/build.sh
./tools/flash.sh
```

The scripts wrap this FQBN:

```
esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,FlashSize=16M,PSRAM=disabled,PartitionScheme=custom
```

### Arduino IDE

Board *ESP32S3 Dev Module*, then USB Mode **USB-OTG (TinyUSB)**, USB CDC On
Boot *Enabled*, Flash Size *16MB*, PSRAM *Disabled*, Partition Scheme
*Custom*. Install **Adafruit GFX Library** and **Adafruit ST7735 and ST7789
Library** from the Library Manager.

### Flashing

First flash: hold the button while plugging the dongle in, then release.

After that, unlock the dongle and stop any active USB tool so its serial interface is visible, then
`./tools/flash.sh` handles flashing. Once the firmware runs, the
serial port belongs to TinyUSB, which does not implement the DTR/RTS reset
esptool expects, so the script opens the port at 1200 baud to trigger
`usb_persist_restart(RESTART_BOOTLOADER)` in the core first.

If the button was held down while plugging in, the dongle stays in the ROM
bootloader: dark screen, no Wi-Fi, silent serial port. Unplug and replug
without touching it.

## First run

By default the dongle starts in insertion-lock standby: screen and LED off, Wi-Fi off,
and only the USB mass-storage interface visible. Unlock with `SSSLL`: three
short presses followed by two long presses. A short press is 30–199 ms;
a long press is at least 200 ms by default. Release the button after each press.
USB briefly reconnects when unlocking to add the keyboard and serial interfaces.

In Settings → Startup & lock, **Start in standby** controls ordinary startup.
It defaults to on and changes take effect on the next boot. Turn it off to start
Wi-Fi, the UI and the keyboard automatically. A pending hard lock overrides this
switch. An armed **Fire after boot** run also keeps startup locked and dark,
regardless of the switch, so only the script's keyboard execution comes up.

The screen shows the network name and password. Both derive from the device
MAC, so every dongle starts with different credentials:

```
MiteByte-8218
flea-A0058218
```

Join the network and the captive portal opens the page. Otherwise go to
`http://192.168.4.1` or `http://mitebyte.local`. Both can be changed in
settings.

Run `00-test-layout.txt` into a text editor on the target machine before
anything else. It types the characters that move between layouts, so a
mismatch is obvious at a glance.

### Getting back in

A mistyped Wi-Fi password would lock you out of the only interface. Hold the
button for ten seconds in any state: settings, pending hard lock and the armed
boot run are cleared. The script library is kept. Unlock with the default
gesture again to reach the built-in network.

### Screen lock and hard lock

Hold the button for two seconds while online, or use the web lock button, to
lock the screen. Wi-Fi, the web interface and keyboard keep running. The screen
stays dark and its on/off, brightness, orientation and credential-display controls
are disabled in the UI. The API also rejects attempts to change these while locked.
The display card provides a short lock notice and an **Unlock screen** button; the
top-bar lock icon also unlocks it. LED controls remain available.
The unlock gesture or web unlock button restores the existing screen preferences;
if Screen is off, it stays off until a normal button tap briefly wakes it.

While insertion-locked or screen-locked, `SSSSS` arms hard lock by default.
It immediately returns to standby: no display or LED, no Wi-Fi/web access,
no HID or serial interface, and any running script is stopped. Mass storage
retains its exposure setting. USB briefly reconnects to remove the other interfaces.
Hard lock does not reboot or consume a reinsertion when it is armed.

The **Hard lock** button under **Startup & lock** arms the same lockout from
the web UI, including while the screen is locked. It works even when the hard-lock
gesture is disabled. The browser receives confirmation and recovery instructions
before the device closes Wi-Fi; a flash-write failure leaves it online and reports
an error. The button uses the saved reinsertion count.

The configured number of subsequent boots remains hard-locked; the following
boot permits unlocking. The default is one locked reinsertion, so replug twice
to recover. An armed boot script stays queued while hard-locked and can run
on the first boot after the lockout ends. Factory reset clears both.

Both gestures, the long-press threshold and the hard-lock count are configurable.
Neither gesture may contain the other, including identical gestures. Settings
saved by older firmware with conflicting gestures fall back to `SSSLL` / `SSSSS`.

## Device tools

The Library contains script scripts and built-in tools. **Wi-Fi Hotspot** shares
the PC's current internet connection over USB to devices on the dongle's Wi-Fi.
Select it to start/stop it and see connection status. **Start automatically after
unlock** is saved separately from armed scripts; startup locks still apply.

The tool reconnects USB as a network adapter and temporarily removes keyboard,
serial and mass-storage interfaces. Stop it to restore normal USB use. Screen
lock leaves sharing running; hard lock stops it. Avoid switching during a USB
drive transfer. While the hotspot runs, open the UI at the dongle's IP address;
the captive portal no longer intercepts internet DNS.

Windows needs Internet Connection Sharing enabled once. Use the optional
[Windows companion](tools/windows/README.md) or configure sharing manually.
**Windows setup → Open setup script** opens `13-windows-hotspot-setup.txt`
in the editor. Run it while the tool is stopped to launch the same installer
through the USB keyboard, with no file transfer. Match the PC keyboard layout,
wait for installation to succeed, and then start the hotspot. Setup runs only
when you run or explicitly arm that script; starting the hotspot does not launch it.
Administrator approval is required for this Windows configuration.
An existing sharing connection to another adapter is left unchanged.

The current implementation supports IPv4 NAT and forwards UDP/TCP DNS. It has
not yet passed physical Windows 10/11 enumeration or end-to-end network tests.
No measured speed or added-ping figures are available. Games that require
incoming port mappings may be affected by the extra NAT layer; there is no
automatic port forwarding. See [device tool architecture](DEVICE_TOOLS.md) for
adding another compiled-in plugin.

## Script commands

| Command | Effect |
|---|---|
| `REM text` | Comment, also `#` and `//` |
| `META windows\|linux\|macos` | Tags the script, shows an OS icon in the library |
| `STRING text` | Types the text |
| `STRINGLN text` | Types the text, then Enter |
| `DELAY n` | Pauses n milliseconds |
| `WAIT_FOR_HOST [ms]` | Waits until the host acknowledges the keyboard, 5000 ms by default |
| `DEFAULTDELAY n` | Implicit pause after every line |
| `DEFAULTCHARDELAY n` | Pause between characters |
| `LAYOUT code` | Switches layout mid-script |
| `REPEAT n` | Replays the previous line n times |
| Named keys | `ENTER` `TAB` `ESC` `SPACE` `BACKSPACE` `DELETE` `INSERT` `HOME` `END` `PAGEUP` `PAGEDOWN` `UP` `DOWN` `LEFT` `RIGHT` `MENU` `CAPSLOCK` `PRINTSCREEN` `PAUSE` `F1`-`F12` |
| Modifiers | `CTRL` `SHIFT` `ALT` `GUI` `ALTGR` then a key: `GUI r`, `CTRL ALT DELETE` |

Layout codes: `us fr de ch hu es it pt br se dk jp`.

Only ASCII is typed. Accented characters in a `STRING` are skipped and
reported in the run log rather than producing a wrong key.

`WAIT_FOR_HOST` replaces the blind `DELAY` most scripts open with. Not
every host sends the report unprompted, so it carries on when the timeout
expires rather than failing the run.

### Layouts and non-Latin input

Layout tables map ASCII to key positions, which is why the list stops where
it does. With a Cyrillic or CJK input mode active, no key produces an ASCII
letter at all, so no table can help. Scripts for those targets switch the
host back to Latin input first, usually with `GUI SPACE`. See
`50-switch-input-language.txt`.

## USB drive

The mass storage interface is always advertised; the setting decides whether
it reports a card. While the card is handed to the host, the file browser in
the settings page refuses. The host gets raw sector access and the firmware
sees a filesystem, and both views cannot be live at once without corrupting
the card.

Format cards as FAT32 or FAT16. The firmware never formats a card by itself,
whatever the mount error.

## Settings

Layout, device name, screen orientation and brightness, status light and its
brightness, Wi-Fi credentials, USB drive, startup standby and lock gestures are stored on internal flash and kept across
reflashing, since the firmware goes to `app0` while settings live on the
`spiffs` partition.

Changing the Wi-Fi credentials restarts the dongle, because the network
being reconfigured is the one carrying the request. Lock settings apply on the
next boot. Display controls preview live while unlocked; **Save display** saves them.

## Status light

The colours are fixed and report state rather than taste. On/off and brightness
are configurable. In insertion-lock or hard-lock standby the LED stays off.

| Light | Meaning |
|---|---|
| Red, slow breath | No device has joined the access point yet |
| Blue, steady | Standby |
| Red, fast pulse | A script is armed or running |
| Green, 5 seconds | The script finished, then back to standby |
| Red, steady 5 seconds | The script stopped on an error |

Colours never cut over. Each change fades the old one out and the new one
in, dipping through black rather than crossing the muddy hues a direct
interpolation would pass through. Both the fades and the breathing use the
same squared curve, since perceived brightness is far from linear and a ramp
that is linear in value reads as a cliff at one end.

## Fire after boot

Arms one run at the next power-up, from the bar under the editor. It arms
the script **as it stands in the editor**, not a reference to a library
entry: edit it and arm what you see. The device keeps its own snapshot in
`/armed.txt`, outside the script directory, so renaming or deleting a
library script leaves the arming alone.

The existence of that file is the armed state. There is no second copy of
that fact to drift out of sync, and the badge reports its size.

It is a single shot: the snapshot is deleted *before* the script is queued,
and nothing is queued unless the delete took, so a crash or a replug during
the run cannot turn one arming into a script that fires on every plug. A
factory reset clears it too.

Selecting a library script alone does not arm a run. Use **Fire after boot**.
That run bypasses the insertion gesture only for keyboard execution: the device
stays locked, dark, and offline until manually unlocked. Hard lock suppresses it.

Execution waits up to five seconds for the USB keyboard interface to become
ready after enumeration; if it does not, the run ends with an error. This wait
is cancellable and does not turn on the screen or Wi-Fi. `WAIT_FOR_HOST` can
add a wait for the keyboard LED report, or use a start delay to allow more time
for the host application and a window to pull the dongle back out.

## Repository layout

| Path | Contents |
|---|---|
| `mitebyte.ino` | Startup, access point, main loop |
| `config.h` | Pins, defaults, limits |
| `macro.h/.cpp` | HID keyboard, layouts, interpreter, run task |
| `storage.h/.cpp` | Script library and settings on LittleFS |
| `ui_display.h/.cpp` | ST7735 screen and APA102 LED |
| `usb_drive.h/.cpp` | Mass storage and card browsing |
| `usb_mode.h/.cpp` | Storage-only and active USB descriptors |
| `tools.h/.cpp` | Built-in tool registry, lifecycle and startup preference |
| `src/tools/<tool>/` | one folder per device tool (e.g. `hotspot/`: USB hotspot, DNS forwarding, packet validation) |
| `src/diagnostics/` | shared SD diagnostic logging (used by the core, driver and tools) |
| `src/usb_ethernet/` | Private TinyUSB application network driver |
| `lock.h/.cpp`, `lock_validation.cpp` | Gesture states and settings validation |
| `web_api.h/.cpp` | HTTP server, API, captive portal |
| `web_assets.h` | Web interface, compiled into the firmware |
| `partitions.csv` | 16 MB layout, 4 MB app, 7.88 MB filesystem |
| `tools/` | Build, flash, release, screen rendering and Windows companion |
| `screens/` | Rendered screen images used by the README |

Scripts run in their own FreeRTOS task, which keeps the web server
answering during a run and makes the progress readout and *Stop* work.
