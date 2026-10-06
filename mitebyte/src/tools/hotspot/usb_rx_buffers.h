#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

// Ethernet input attaches our buffer to a custom pbuf. It can outlive the
// receive call, so credits return only through driver_free_rx_buffer.
// Never reset the pool on USB reconnect: lwIP can still own an old packet.
class UsbRxBuffers {
public:
  static constexpr unsigned Capacity = 4;
  static constexpr size_t FrameSize = 1514;
  // Pause RX this far above empty. lwIP/Wi-Fi keep allocating between our
  // per-frame checks while forwarding a download burst, so a 12 KiB floor
  // overshot to <1 KiB on hardware. A larger cushion makes the worker stop
  // pulling from USB sooner, capping the burst; near-free at USB Full Speed.
  static constexpr size_t HeapReserve = 24 * 1024;
  static constexpr size_t AllocationReserve = 2048;
  // Above this much free internal heap, the costly largest-free-block walk may
  // be skipped and a recent reading reused (see acquireReceiveBuffer).
  static constexpr size_t ComfortableHeap = 32 * 1024;

  void *acquire(size_t freeHeap, size_t largestBlock) {
    if (freeHeap < HeapReserve || largestBlock < AllocationReserve) return nullptr;
    uint32_t mask = used.load();
    for (;;) {
      unsigned index = 0;
      while (index < Capacity && (mask & (1u << index))) ++index;
      if (index == Capacity) return nullptr;
      if (used.compare_exchange_weak(mask, mask | (1u << index))) return storage[index].bytes;
    }
  }
  bool release(void *buffer) {
    for (unsigned index = 0; index < Capacity; ++index) {
      if (buffer == storage[index].bytes) {
        return (used.fetch_and(~(1u << index)) & (1u << index)) != 0;
      }
    }
    return false;
  }
  unsigned inUse() const {
    uint32_t mask = used.load();
    unsigned count = 0;
    while (mask) { count += mask & 1u; mask >>= 1; }
    return count;
  }
  // No free slot: acquire() would fail regardless of heap, so callers can skip
  // any heap inspection entirely while the pool is drained by lwIP.
  bool full() const {
    constexpr uint32_t all = (1u << Capacity) - 1;
    return (used.load() & all) == all;
  }
private:
  struct alignas(4) Buffer { uint8_t bytes[FrameSize]; };
  Buffer storage[Capacity] = {};
  std::atomic<uint32_t> used{0};
};
