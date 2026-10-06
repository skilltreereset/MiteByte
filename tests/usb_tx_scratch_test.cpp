#include "../mitebyte/src/tools/hotspot/usb_tx_scratch.h"
#include <cassert>
#include <cstdio>
#include <mutex>
#include <vector>
#include <thread>
#include <cstring>

struct FakeSemaphore { std::mutex mutex; };
struct FakeQueue { size_t capacity; std::mutex mutex; std::vector<HotspotFrame> frames; };
static bool allocationFails = false;
SemaphoreHandle_t xSemaphoreCreateMutex() { return allocationFails ? nullptr : new FakeSemaphore; }
int xSemaphoreTake(SemaphoreHandle_t lock, uint32_t) { lock->mutex.lock(); return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t lock) { lock->mutex.unlock(); return pdTRUE; }
int xQueueSend(QueueHandle_t queue, const void *data, uint32_t timeout) {
  assert(timeout == 0); // never wait for queue space while holding scratch
  std::lock_guard<std::mutex> lock(queue->mutex);
  if (queue->frames.size() == queue->capacity) return 0;
  std::this_thread::yield();
  queue->frames.push_back(*static_cast<const HotspotFrame *>(data)); return pdTRUE;
}
static void verify(const HotspotFrame &frame) {
  assert(frame.size == 14 + frame.generation % 1501);
  for (unsigned n = 0; n < frame.size; ++n) assert(frame.bytes[n] == uint8_t(frame.generation + n));
}
int main() {
  static UsbTxScratch scratch;
  FakeQueue queue; queue.capacity = 800;
  uint8_t bytes[1515] = {};
  assert(!scratch.enqueue(&queue, bytes, 14, 1));
  allocationFails = true; assert(!scratch.begin());
  allocationFails = false; assert(scratch.begin() && scratch.begin());
  assert(!scratch.enqueue(&queue, bytes, 13, 1));
  assert(!scratch.enqueue(&queue, bytes, 1515, 1));
  for (unsigned size : {14u, 42u, 1514u}) {
    std::memset(bytes, size & 255, size);
    assert(scratch.enqueue(&queue, bytes, size, size));
    std::memset(bytes, 0, size); // queue must own its bytes, not reference scratch/caller
    const auto &frame = queue.frames.back();
    assert(frame.size == size && frame.generation == size);
    for (unsigned n = 0; n < size; ++n) assert(frame.bytes[n] == uint8_t(size));
  }
  queue.frames.clear();
  std::vector<std::thread> writers;
  for (unsigned writer = 0; writer < 8; ++writer) writers.emplace_back([&, writer] {
    uint8_t packet[1514];
    for (unsigned n = 0; n < 100; ++n) {
      unsigned generation = writer * 100 + n;
      unsigned size = 14 + generation % 1501;
      for (unsigned i = 0; i < size; ++i) packet[i] = uint8_t(generation + i);
      assert(scratch.enqueue(&queue, packet, size, generation));
    }
  });
  for (auto &writer : writers) writer.join();
  assert(queue.frames.size() == 800);
  for (const auto &frame : queue.frames) verify(frame);
  assert(!scratch.enqueue(&queue, bytes, 14, 0)); // bounded queue failure releases scratch
  queue.frames.clear();
  assert(scratch.enqueue(&queue, bytes, 14, 0));
  puts("PASS: USB transmit scratch, concurrent frame integrity, queue ownership, full queue and startup failure");
}
