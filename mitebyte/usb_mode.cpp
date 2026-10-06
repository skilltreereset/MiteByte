#include "usb_mode.h"
#include "config.h"
#include "src/diagnostics/diag_log.h"
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "esp32-hal-tinyusb.h"
#include <esp_mac.h>
#include <atomic>
#include <cstring>
#include <new>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#if CONFIG_IDF_TARGET_ESP32S3
#include <soc/usb_dwc_struct.h>
#endif
#include "device/usbd_pvt.h"

// Arduino registers interfaces in global constructors and starts USB before
// setup(). Delaying Keyboard.begin() cannot hide HID. Override the core's
// weak descriptor callbacks instead; storage-only must be the initial value.
static std::atomic<bool> s_active{false};
static std::atomic<const UsbToolProfile *> s_tool{nullptr};
static TaskHandle_t s_usbTask = nullptr;
static unsigned s_normalPriority = 0;

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
#define USB_PRODUCT "MiteByte"
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
  const UsbToolProfile *tool = s_tool.load();
  if (tool) return tool->configuration;
  return s_active.load() ? s_activeConfig : s_storageConfig;
}

extern "C" const uint8_t *tud_descriptor_device_cb() {
  const UsbToolProfile *tool = s_tool.load();
  if (tool) return tool->device;
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
  const UsbToolProfile *tool = s_tool.load();
  const char *value;
  switch (index) {
    case 1: value = USB_MANUFACTURER; break;
    case 2: value = tool ? tool->product : USB_PRODUCT; break;
    case 3: {
      uint8_t mac[6];
      esp_efuse_mac_get_default(mac);
      // Distinct stable USB instances prevent a host reusing cached keyboard
      // or serial interfaces for the storage-only configuration.
      snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X-%c",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
               tool ? tool->serialSuffix : s_active.load() ? 'A' : 'S');
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

// Two transition paths. When a tool profile is loaded, record the desired
// storage/active state and hand off to usbModeSetToolProfile(nullptr), which
// performs the controller teardown and rebuild on the USB task. Otherwise
// (storage <-> active) toggle the descriptor set with a plain reconnect here.
void usbModeSetActive(bool active) {
  if (s_active.load() == active && !s_tool.load()) return;
  if (s_tool.load()) {
    s_active.store(active);
    usbModeSetToolProfile(nullptr);
    return;
  }
  tud_disconnect();
  delay(250);  // let the host observe removal before changing descriptors
  s_tool.store(nullptr);
  s_active.store(active);
  tud_connect();
}

// Shared between the caller and the deferred USB-task callback. Heap-owned so
// the caller can time out without freeing state the callback may still touch:
// each side releases one reference when done, and whoever drops it to zero frees.
struct UsbProfileSwitch {
  const UsbToolProfile *profile = nullptr;
  SemaphoreHandle_t done = nullptr;
  bool success = false;
  std::atomic<int> owners{2}; // caller + deferred callback
};
static void releaseSwitch(UsbProfileSwitch *request) {
  if (request->owners.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    if (request->done) vSemaphoreDelete(request->done);
    delete request;
  }
}
// A controller teardown/rebuild should take well under a second (two 250 ms
// settles plus init). Bound the wait far above that so a wedged controller
// fails the switch instead of hanging the caller forever.
static constexpr uint32_t kSwitchTimeoutMs = 5000;
static void switchToolProfile(void *argument) {
  auto *request = static_cast<UsbProfileSwitch *>(argument);
  hotspotDiagnosticLog("USB_SWITCH begin profile=%c initialized=%d", request->profile ? request->profile->serialSuffix : 'A', tud_inited());
  if (!s_usbTask) {
    s_usbTask = xTaskGetCurrentTaskHandle();
    s_normalPriority = uxTaskPriorityGet(s_usbTask);
  }
  // Arduino starts USB above even the Wi-Fi driver. That suits brief HID
  // reports, but a continuous network stream must yield to Wi-Fi and lwIP.
  if (request->profile) vTaskPrioritySet(s_usbTask, CONFIG_LWIP_TCPIP_TASK_PRIO - 1);
  // Controller teardown and class callbacks must run in tud_task's context.
  // A pull-up toggle alone leaves endpoint and pending-transfer state alive.
  tud_disconnect();
  bool stopped = tud_deinit(0);
  hotspotDiagnosticLog("USB_SWITCH deinit=%d", stopped);
  delay(250);
  s_tool.store(request->profile);
#if CONFIG_IDF_TARGET_ESP32S3
  // TinyUSB 0.21 deinit gates HCLK and stops the PHY clock. Its ESP32
  // clock_init hook is empty and core_init resets before clearing PCGCCTL.
  // Restore both clocks first, otherwise that reset starts on a gated core.
  if (stopped) USB_DWC.pcgcctl_reg.val &= ~uint32_t(3);
#endif
  const tusb_rhport_init_t init = { TUSB_ROLE_DEVICE, TUSB_SPEED_FULL };
  request->success = stopped && tud_rhport_init(0, &init);
  hotspotDiagnosticLog("USB_SWITCH init=%d priority=%u", request->success, unsigned(uxTaskPriorityGet(s_usbTask)));
  if (!request->profile) vTaskPrioritySet(s_usbTask, s_normalPriority);
  xSemaphoreGive(request->done);
  releaseSwitch(request); // may free if the caller already timed out and left
}
bool usbModeSetToolProfile(const UsbToolProfile *profile) {
  if (s_tool.load() == profile) return tud_inited();
  auto *request = new (std::nothrow) UsbProfileSwitch;
  if (!request) return false;
  request->profile = profile;
  request->done = xSemaphoreCreateBinary();
  if (!request->done) { delete request; return false; }
  usbd_defer_func(switchToolProfile, request, false);
  bool completed = xSemaphoreTake(request->done, pdMS_TO_TICKS(kSwitchTimeoutMs)) == pdTRUE;
  bool success = completed && request->success;
  if (!completed) hotspotDiagnosticLog("USB_SWITCH timeout profile=%c", profile ? profile->serialSuffix : 'A');
  releaseSwitch(request); // the late callback, if any, frees when it also releases
  return success;
}

bool usbModeKeyboardReady() {
  return s_active.load() && !s_tool.load() && tud_hid_ready();
}

unsigned usbModeTaskPriority() { return s_usbTask ? uxTaskPriorityGet(s_usbTask) : 0; }
unsigned usbModeTaskStackFree() { return s_usbTask ? uxTaskGetStackHighWaterMark(s_usbTask) : 0; }
