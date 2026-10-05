# Regression checks

Run from the repository root:

```sh
python tests/run_native.py
node tests/web_ui_test.mjs
python tests/check_usb_descriptors.py .pio/build/fleabyte/firmware.elf
```

The native check needs g++ or clang++ and compiles the production lock,
validation and display sources against fake hardware IO. It checks gesture
conflicts, button holds, screen light gating, immediate LED blanking, queued UI
hard lock and persistence failures, automatic startup with standby disabled,
hard-lock precedence and reset.

The JavaScript check runs the script embedded in `web_assets.h` with a mocked
DOM/API. It checks disabled screen controls, both unlock actions, LED-only requests
while locked, conflicting gestures, the startup switch, reset errors, and hard-lock recovery and errors.
It does not verify browser layout or the firmware's HTTP server.

The descriptor check needs pyelftools (included in the local PlatformIO Python
environment) and a built ELF. It inspects the linked descriptor bytes and verifies
that the firmware overrides the framework's weak callbacks. It is not a physical
USB enumeration test.

On hardware, check cold locked boot, gesture unlock, screen settings while
locked, hard lock during a harmless test payload, reinsertion counts, and an
armed boot run with display/Wi-Fi off. USB reconnects when changing interface
sets; perform these checks without a host file transfer in progress.
