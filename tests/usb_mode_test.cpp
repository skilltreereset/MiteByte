#include <cassert>
#include <cstdio>
#include <vector>
#include <tusb.h>
#include <class/hid/hid_device.h>
#include <device/usbd_pvt.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <soc/usb_dwc_struct.h>
#include "../fleabyte/usb_mode.h"

static bool usbContext = false, failAllocation = false, failInit = false;
static std::vector<int> events;
static const uint8_t *expectedDevice;
static unsigned priority = 24;
usb_dwc_dev_t USB_DWC;
extern "C" void hotspotDiagnosticLog(const char *, ...) {}
TaskHandle_t xTaskGetCurrentTaskHandle() { assert(usbContext); return reinterpret_cast<void *>(1); }
unsigned uxTaskPriorityGet(TaskHandle_t task) { assert(task); return priority; }
void vTaskPrioritySet(TaskHandle_t task, unsigned value) { assert(task && usbContext); priority = value; }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task) { assert(task); return 1024; }
SemaphoreHandle_t xSemaphoreCreateBinary() { return failAllocation ? nullptr : new bool(false); }
void xSemaphoreGive(SemaphoreHandle_t sem) { *sem = true; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, unsigned) { assert(*sem); return pdTRUE; }
void vSemaphoreDelete(SemaphoreHandle_t sem) { delete sem; }
void delay(uint32_t ms) { assert(ms == 250); events.push_back(3); }
extern "C" bool tud_disconnect() { events.push_back(1); return true; }
extern "C" bool tud_connect() { events.push_back(5); return true; }
extern "C" bool tud_inited() { return !failInit; }
extern "C" bool tud_deinit(uint8_t port) {
  assert(usbContext && port == 0); events.push_back(2);
  USB_DWC.pcgcctl_reg.val = 0x83; // stopped clocks; unrelated bits must survive
  return true;
}
extern "C" bool tud_rhport_init(uint8_t port, const tusb_rhport_init_t *init) {
  assert(usbContext && port == 0 && init->role == TUSB_ROLE_DEVICE && init->speed == TUSB_SPEED_FULL);
  assert(USB_DWC.pcgcctl_reg.val == 0x80); // wake clocks before core initialization
  assert(tud_descriptor_device_cb() == expectedDevice || !expectedDevice);
  events.push_back(4); return !failInit;
}
extern "C" void usbd_defer_func(osal_task_func_t function, void *argument, bool isr) {
  assert(!usbContext && !isr); usbContext = true; function(argument); usbContext = false;
}
extern "C" bool tud_hid_n_ready(uint8_t instance) { assert(instance == 0); return true; }
int main() {
  static const uint8_t configuration[] = {9,2}, device[] = {18,1};
  const UsbToolProfile profile = {configuration, device, "Test hotspot", 'H'};
  usbModeSetActive(true); events.clear();
  for (int cycle = 0; cycle < 10; ++cycle) {
    expectedDevice = device;
    assert(usbModeSetToolProfile(&profile));
    assert(priority == 17 && usbModeTaskPriority() == 17);
    assert(usbModeTaskStackFree() == 1024);
    assert(events == std::vector<int>({1,2,3,4}));
    assert(!usbModeKeyboardReady());
    events.clear();
    assert(usbModeSetToolProfile(&profile)); assert(events.empty());
    expectedDevice = nullptr;
    assert(usbModeSetToolProfile(nullptr));
    assert(priority == 24);
    assert(events == std::vector<int>({1,2,3,4}));
    assert(usbModeKeyboardReady()); events.clear();
  }
  failAllocation = true;
  assert(!usbModeSetToolProfile(&profile)); assert(events.empty());
  failAllocation = false; failInit = true; expectedDevice = device;
  assert(!usbModeSetToolProfile(&profile)); assert(events == std::vector<int>({1,2,3,4}));
  puts("PASS: production USB mode switching in USB task, controller reset, descriptors, ten restarts and failures");
}
