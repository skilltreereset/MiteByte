#pragma once
#include <stdint.h>

// A built-in tool supplies a complete USB profile. The core only switches
// profiles; endpoint ownership and class drivers belong to the tool.
struct UsbToolProfile {
  const uint8_t *configuration;
  const uint8_t *device;
  const char *product;
  char serialSuffix;
};
bool usbModeSetToolProfile(const UsbToolProfile *profile);

// Startup is storage-only, including before setup(). Active mode adds HID
// and (when built with CDC on boot) the serial port. Changing mode briefly
// disconnects USB so the host reads the new descriptors.
void usbModeSetActive(bool active);
bool usbModeKeyboardReady();
unsigned usbModeTaskPriority();
unsigned usbModeTaskStackFree();
