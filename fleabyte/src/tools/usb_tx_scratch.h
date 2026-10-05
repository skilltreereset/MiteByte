#pragma once
#include "usb_rx_backpressure.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// lwIP output can run on the calling socket task with core locking enabled.
// Keep the Ethernet frame off that task's stack. Multiple callers may use this
// scratch, but the lock covers only copying and a nonblocking queue send.
class UsbTxScratch {
public:
  bool begin() {
    if (!lock) lock = xSemaphoreCreateMutex();
    return lock != nullptr;
  }
  bool enqueue(QueueHandle_t queue, const void *data, size_t size, uint32_t generation) {
    if (!lock || !queue || !data || size < 14 || size > sizeof(frame.bytes)) return false;
    if (xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE) return false;
    frame.generation = generation;
    frame.size = uint16_t(size);
    std::memcpy(frame.bytes, data, size);
    bool queued = xQueueSend(queue, &frame, 0) == pdTRUE;
    xSemaphoreGive(lock);
    return queued;
  }
private:
  SemaphoreHandle_t lock = nullptr;
  HotspotFrame frame = {};
};
