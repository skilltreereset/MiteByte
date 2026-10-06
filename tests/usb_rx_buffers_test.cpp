#include "../mitebyte/src/tools/hotspot/usb_rx_buffers.h"
#include "../mitebyte/src/tools/hotspot/usb_rx_backpressure.h"
#include <cassert>
#include <cstring>
#include <deque>
#include <thread>
#include <vector>
#include <cstdio>

int main() {
  UsbRxBuffers pool;
  void *owned[UsbRxBuffers::Capacity];
  assert(!pool.full());
  assert(!pool.acquire(UsbRxBuffers::HeapReserve - 1, 65536));
  assert(!pool.acquire(65536, UsbRxBuffers::AllocationReserve - 1));
  for (unsigned i = 0; i < UsbRxBuffers::Capacity; ++i) {
    owned[i] = pool.acquire(65536, 65536);
    assert(owned[i] && reinterpret_cast<uintptr_t>(owned[i]) % 4 == 0);
    std::memset(owned[i], i + 1, UsbRxBuffers::FrameSize);
  }
  assert(pool.inUse() == UsbRxBuffers::Capacity && !pool.acquire(65536, 65536));
  // full() lets the hot path skip the heap walk once every slot is out to lwIP.
  assert(pool.full());
  // Releasing one completed pbuf must not overwrite packets still held by lwIP.
  assert(pool.release(owned[1]));
  assert(!pool.full());
  assert(!pool.release(owned[1]));
  assert(!pool.release(nullptr));
  assert(!pool.release(static_cast<uint8_t *>(owned[0]) + 1));
  void *next = pool.acquire(65536, 65536);
  assert(next == owned[1]);
  for (unsigned i = 0; i < UsbRxBuffers::Capacity; ++i) {
    assert(static_cast<uint8_t *>(owned[i])[1513] == i + 1);
    assert(pool.release(owned[i]));
  }

  // Saturate both stages while the network retains its pbufs. The USB host
  // must pause, resume in order and preserve each full-size frame unchanged.
  UsbRxBackpressure usb;
  std::deque<HotspotFrame> queue;
  std::deque<void *> network;
  unsigned renewals = 0;
  auto enqueue = [&](const HotspotFrame &frame) {
    if (queue.size() == 2) return false;
    queue.push_back(frame); return true;
  };
  auto renew = [&] { ++renewals; };
  for (unsigned number = 1; number <= 10000; ++number) {
    uint8_t packet[1514];
    std::memset(packet, number % 251, sizeof(packet));
    assert(usb.accept(packet, sizeof(packet), enqueue, renew, number));
    // Model upper-layer delivery without releasing the owned buffers yet.
    while (!queue.empty()) {
      void *buffer = pool.acquire(65536, 65536);
      if (!buffer) break;
      std::memcpy(buffer, queue.front().bytes, sizeof(packet));
      network.push_back(buffer); queue.pop_front();
    }
    if (usb.waiting()) {
      assert(pool.inUse() == UsbRxBuffers::Capacity);
      assert(queue.size() == 2);
      assert(pool.release(network.front())); network.pop_front();
      void *buffer = pool.acquire(65536, 65536); assert(buffer);
      std::memcpy(buffer, queue.front().bytes, sizeof(packet));
      network.push_back(buffer); queue.pop_front();
      usb.retry(enqueue, renew);
      assert(!usb.waiting());
      assert(queue.back().generation == number);
      assert(queue.back().bytes[0] == number % 251 && queue.back().bytes[1513] == number % 251);
    }
    assert(pool.inUse() <= UsbRxBuffers::Capacity && queue.size() <= 2);
  }
  assert(renewals == 10000);
  for (void *buffer : network) assert(pool.release(buffer));
  assert(pool.inUse() == 0);

  // Receive task and TCP/IP release callbacks run independently. Check that
  // concurrent borrowers never receive the same outstanding payload buffer.
  std::vector<std::thread> threads;
  for (unsigned worker = 0; worker < 8; ++worker) threads.emplace_back([&, worker] {
    for (unsigned packet = 0; packet < 2000; ++packet) {
      void *buffer;
      while (!(buffer = pool.acquire(65536, 65536))) std::this_thread::yield();
      std::memset(buffer, worker + 1, UsbRxBuffers::FrameSize);
      std::this_thread::yield();
      for (unsigned i = 0; i < UsbRxBuffers::FrameSize; ++i)
        assert(static_cast<uint8_t *>(buffer)[i] == worker + 1);
      assert(pool.release(buffer));
    }
  });
  for (auto &thread : threads) thread.join();
  assert(pool.inUse() == 0);
  puts("PASS: bounded RX ownership, heap reserve, burst backpressure and concurrent release");
}
