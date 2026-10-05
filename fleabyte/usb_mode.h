#pragma once

// Startup is storage-only, including before setup(). Active mode adds HID
// and (when built with CDC on boot) the serial port. Changing mode briefly
// disconnects USB so the host reads the new descriptors.
void usbModeSetActive(bool active);
bool usbModeKeyboardReady();
