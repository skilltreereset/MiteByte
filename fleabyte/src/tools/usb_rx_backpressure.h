#pragma once
#include <cstdint>
#include <cstring>
#include <atomic>

struct HotspotFrame { uint32_t generation; uint16_t size; uint8_t bytes[1514]; };

// Called exclusively from the TinyUSB task. Hold one copied frame when the
// queue is full; leave USB OUT unarmed so the host retries instead of losing it.
class UsbRxBackpressure {
public:
  template <typename Enqueue, typename Renew>
  bool accept(const uint8_t *data, uint16_t size, Enqueue enqueue, Renew renew, uint32_t generation = 0) {
    if (pending || size < 14 || size > sizeof(frame.bytes)) return false;
    frame.size = size;
    frame.generation = generation;
    memcpy(frame.bytes, data, size);
    pending = !enqueue(frame);
    if (!pending) renew();
    return true;
  }
  template <typename Enqueue, typename Renew>
  void retry(Enqueue enqueue, Renew renew) {
    if (pending && enqueue(frame)) { pending = false; renew(); }
  }
  void cancel() { pending = false; }
  bool waiting() const { return pending.load(); }
private:
  HotspotFrame frame = {};
  std::atomic<bool> pending{false};
};
