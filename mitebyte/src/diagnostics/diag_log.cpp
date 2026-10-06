#include "diag_log.h"
#include "../../usb_drive.h"
#include "diag_packet.h"
#include <Arduino.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>

#if HOTSPOT_DIAGNOSTICS
struct Record { uint32_t timestamp; bool barrier; char text[184]; };
static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_flushed;
static std::atomic<bool> s_enabled{false}, s_written{false}, s_failed{false};
static std::atomic<unsigned> s_dropped{0};
static std::atomic<unsigned> s_writers{0};
static bool s_previousExposure;
static void writer(void *) {
  Record record;
  bool first = true;
  for (;;) {
    if (xQueueReceive(s_queue, &record, portMAX_DELAY) != pdTRUE) continue;
    char line[216];
    int size = snprintf(line, sizeof(line), "%lu %s\n", (unsigned long)record.timestamp, record.text);
    bool ok = usbDriveDiagnosticAppend(line, size_t(size), first);
    if (ok) { first = false; s_written = true; }
    else s_failed = true;
    if (record.barrier) { first = true; xSemaphoreGive(s_flushed); }
  }
}
bool hotspotDiagnosticsBegin() {
  s_written = false; s_failed = false; s_dropped = 0;
  if (!s_queue) s_queue = xQueueCreate(24, sizeof(Record));
  if (!s_flushed) s_flushed = xSemaphoreCreateBinary();
  if (!s_queue || !s_flushed) { s_failed = true; return false; }
  if (!s_task && xTaskCreate(writer, "hotspot-log", 4096, nullptr, 1, &s_task) != pdPASS) {
    s_failed = true; return false;
  }
  s_previousExposure = usbDriveExposureRequested();
  usbDriveSetExposed(false);
  if (!usbDriveFsAvailable()) {
    s_failed = true;
    if (s_previousExposure) usbDriveSetExposed(true);
    return false;
  }
  s_enabled = true;
  hotspotDiagnosticLog("SESSION firmware=%s %s reset=%d heap=%u min_heap=%u", __DATE__, __TIME__,
      int(esp_reset_reason()), unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  return true;
}
void hotspotDiagnosticsEnd() {
  if (!s_enabled.load()) return;
  s_enabled = false;
  while (s_writers.load()) vTaskDelay(1);
  Record barrier = {}; barrier.barrier = true;
  barrier.timestamp = millis();
  snprintf(barrier.text, sizeof(barrier.text), "SESSION_END log_queue_drops=%u", s_dropped.load());
  xQueueSend(s_queue, &barrier, portMAX_DELAY);
  xSemaphoreTake(s_flushed, portMAX_DELAY);
  // The barrier follows every append; all files are closed before MSC sees it.
  if (s_previousExposure) usbDriveSetExposed(true);
}
extern "C" void hotspotDiagnosticLog(const char *format, ...) {
  struct Producer { Producer() { ++s_writers; } ~Producer() { --s_writers; } } producer;
  if (!s_enabled.load() || !s_queue) return;
  Record record = {}; record.timestamp = millis();
  va_list args; va_start(args, format);
  vsnprintf(record.text, sizeof(record.text), format, args); va_end(args);
  if (xQueueSend(s_queue, &record, 0) != pdTRUE) ++s_dropped;
}
extern "C" void hotspotDiagnosticPacket(const char *direction, const uint8_t *data, size_t size) {
  if (!s_enabled.load()) return;
  char summary[164];
  if (hotspotDiagnosticPacketSummary(data, size, summary, sizeof(summary)))
    hotspotDiagnosticLog("%s %s", direction, summary);
}
const char *hotspotDiagnosticsState() {
  return s_failed.load() ? "SD write unavailable" : s_written.load() ?
    (s_enabled.load() ? "Recording to SD card" : "Saved on SD card") : s_enabled.load() ? "Starting" : "Not recorded";
}
unsigned hotspotDiagnosticsDropped() { return s_dropped.load(); }
#else  // HOTSPOT_DIAGNOSTICS == 0: compile the subsystem out entirely.
extern "C" void hotspotDiagnosticLog(const char *, ...) {}
extern "C" void hotspotDiagnosticPacket(const char *, const uint8_t *, size_t) {}
bool hotspotDiagnosticsBegin() { return true; }
void hotspotDiagnosticsEnd() {}
const char *hotspotDiagnosticsState() { return "Disabled"; }
unsigned hotspotDiagnosticsDropped() { return 0; }
#endif
