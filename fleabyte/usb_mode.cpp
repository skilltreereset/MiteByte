#include "usb_mode.h"
#include "config.h"
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "esp32-hal-tinyusb.h"
#include <esp_mac.h>
#include <atomic>
#include <cstring>

// Arduino registers interfaces in global constructors and starts USB before
// setup(). Delaying Keyboard.begin() cannot hide HID. Override the core's
// weak descriptor callbacks instead; storage-only must be the initial value.
static std::atomic<bool> s_active{false};

// Match Arduino's endpoint reservations: keyboard 1, MSC 2, CDC OUT 3,
// CDC IN 4 and CDC notification 5. The same MSC interface survives in both
// configurations; class callbacks still belong to Arduino's USB drivers.
static const uint8_t s_keyboardReport[] = {
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_REPORT_ID_KEYBOARD))
};
static const uint8_t s_storageConfig[] = {
  TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN, 0, 500),
  TUD_MSC_DESCRIPTOR(0, 0, 0x02, 0x82, CFG_TUD_ENDPOINT_SIZE)
};
static const uint8_t s_activeConfig[] = {
#if ARDUINO_USB_CDC_ON_BOOT
  TUD_CONFIG_DESCRIPTOR(1, 4, 0,
      TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN + TUD_HID_INOUT_DESC_LEN + TUD_CDC_DESC_LEN, 0, 500),
#else
  TUD_CONFIG_DESCRIPTOR(1, 2, 0,
      TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN + TUD_HID_INOUT_DESC_LEN, 0, 500),
#endif
  TUD_MSC_DESCRIPTOR(0, 0, 0x02, 0x82, CFG_TUD_ENDPOINT_SIZE),
  TUD_HID_INOUT_DESCRIPTOR(1, 0, HID_ITF_PROTOCOL_KEYBOARD,
      sizeof(s_keyboardReport), 0x01, 0x81, CFG_TUD_ENDPOINT_SIZE, 1),
#if ARDUINO_USB_CDC_ON_BOOT
  TUD_CDC_DESCRIPTOR(2, 0, 0x85, CFG_TUD_ENDPOINT_SIZE, 0x03, 0x84, CFG_TUD_ENDPOINT_SIZE)
#endif
};

#ifndef USB_VID
#define USB_VID USB_ESPRESSIF_VID
#endif
#ifndef USB_PID
#define USB_PID 0x0002
#endif
#ifndef USB_PRODUCT
#define USB_PRODUCT "Fleabyte"
#endif
#ifndef USB_MANUFACTURER
#define USB_MANUFACTURER "LilyGO"
#endif

static const tusb_desc_device_t s_storageDevice = {
  sizeof(tusb_desc_device_t), TUSB_DESC_DEVICE, 0x0200,
  0, 0, 0, CFG_TUD_ENDPOINT0_SIZE,
  USB_VID, USB_PID, 0x0100, 1, 2, 3, 1
};
static const tusb_desc_device_t s_activeDevice = {
  sizeof(tusb_desc_device_t), TUSB_DESC_DEVICE, 0x0200,
#if ARDUINO_USB_CDC_ON_BOOT
  TUSB_CLASS_MISC, MISC_SUBCLASS_COMMON, MISC_PROTOCOL_IAD,
#else
  0, 0, 0,
#endif
  CFG_TUD_ENDPOINT0_SIZE,
  USB_VID, USB_PID, 0x0100, 1, 2, 3, 1
};

extern "C" const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  if (index != 0) return nullptr;
  return s_active.load() ? s_activeConfig : s_storageConfig;
}

extern "C" const uint8_t *tud_descriptor_device_cb() {
  return reinterpret_cast<const uint8_t *>(s_active.load() ? &s_activeDevice : &s_storageDevice);
}

extern "C" const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  static uint16_t descriptor[64];
  if (index == 0) {
    descriptor[0] = (TUSB_DESC_STRING << 8) | 4;
    descriptor[1] = 0x0409;
    return descriptor;
  }

  char serial[24];
  const char *value;
  switch (index) {
    case 1: value = USB_MANUFACTURER; break;
    case 2: value = USB_PRODUCT; break;
    case 3: {
      uint8_t mac[6];
      esp_efuse_mac_get_default(mac);
      // Distinct stable USB instances prevent a host reusing cached keyboard
      // or serial interfaces for the storage-only configuration.
      snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X-%c",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], s_active.load() ? 'A' : 'S');
      value = serial;
      break;
    }
    default: return nullptr;
  }
  size_t count = strlen(value);
  if (count > 63) count = 63;
  for (size_t i = 0; i < count; i++) descriptor[i + 1] = (uint8_t)value[i];
  descriptor[0] = (TUSB_DESC_STRING << 8) | (2 * count + 2);
  return descriptor;
}

void usbModeSetActive(bool active) {
  if (s_active.load() == active) return;
  tud_disconnect();
  delay(250);  // let the host observe removal before changing descriptors
  s_active.store(active);
  tud_connect();
}

bool usbModeKeyboardReady() {
  return s_active.load() && tud_hid_ready();
}
