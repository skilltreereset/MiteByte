#include <cassert>
#include <cstdio>
#include <map>
#include "tools.h"
#include "../mitebyte/src/usb_ethernet/rndis_validate.h"
#include "../mitebyte/src/tools/hotspot/dns_packet.h"
#include "../mitebyte/src/tools/hotspot/net_rules.h"
#include "../mitebyte/src/tools/hotspot/usb_rx_backpressure.h"
#include <deque>

static String testStartup;
static bool testStorageFail = false;
String storageToolStartupRead() { return testStartup; }
bool storageToolStartupWrite(const String &id) {
  if (testStorageFail) return false;
  testStartup = id; return true;
}
static int starts = 0, stops = 0, ticks = 0;
static bool rejectStart = false;
static void begin() {}
static bool start(String &error) {
  if (rejectStart) { error = "Start failed"; return false; }
  ++starts; return true;
}
static void stop() { ++stops; }
static void tick() { ++ticks; }
static ToolStatus status() { return {toolsRunning(), "idle", "test", {}}; }
extern const ToolPlugin USB_HOTSPOT_TOOL = {
  "usb-hotspot", "test", "test", begin, start, stop, tick, status,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};
static bool lightStart(String &) { return true; }
static void noop() {}
static ToolStatus lightStatus() { return {false, "stopped", "test", {}}; }
extern const ToolPlugin SCREEN_LIGHT_TOOL = {
  "screen-light", "test", "test", begin, lightStart, noop, noop, lightStatus,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};
static void put32(uint8_t *p, uint32_t value) {
  for (int i = 0; i < 4; ++i) p[i] = value >> (8 * i);
}
int main() {
  {
    UsbRxBackpressure rx;
    std::deque<HotspotFrame> queue;
    int renewed = 0;
    auto enqueue = [&](const HotspotFrame &frame) {
      if (queue.size() == 2) return false;
      queue.push_back(frame); return true;
    };
    auto renew = [&]() { ++renewed; };
    uint8_t bytes[14] = {};
    bytes[0] = 1; assert(rx.accept(bytes, sizeof(bytes), enqueue, renew));
    assert(!rx.waiting()); // no USB retry event for an immediately queued frame
    bytes[0] = 2; assert(rx.accept(bytes, sizeof(bytes), enqueue, renew));
    bytes[0] = 3; assert(rx.accept(bytes, sizeof(bytes), enqueue, renew, 42));
    assert(renewed == 2); // third frame pauses USB rather than being discarded
    assert(rx.waiting());
    bytes[0] = 99; // deferred frame must own a copy of the USB buffer
    rx.retry(enqueue, renew); assert(renewed == 2);
    assert(queue.front().bytes[0] == 1); queue.pop_front();
    rx.retry(enqueue, renew); assert(renewed == 3);
    assert(!rx.waiting());
    assert(queue.front().bytes[0] == 2); queue.pop_front();
    assert(queue.front().bytes[0] == 3 && queue.front().generation == 42); queue.pop_front();
    assert(!rx.accept(bytes, 13, enqueue, renew));
    assert(!rx.accept(bytes, 1515, enqueue, renew));
    enqueue(HotspotFrame{}); enqueue(HotspotFrame{});
    assert(rx.accept(bytes, sizeof(bytes), enqueue, renew));
    rx.cancel(); queue.clear(); rx.retry(enqueue, renew);
    assert(!rx.waiting());
    assert(queue.empty() && renewed == 3); // stop must not resume an old frame
  }
  testStartup = "usb-hotspot";
  toolsBegin();
  String error;
  assert(!toolsStart("usb-hotspot", error)); // locked
  toolsTick(false); assert(starts == 0);
  toolsOnline(); toolsTick(true); assert(starts == 0); // armed script takes precedence
  toolsTick(false); assert(starts == 1 && toolsRunning());
  assert(toolsStart("usb-hotspot", error) && starts == 1); // idempotent
  assert(!toolsStart("missing", error));
  toolsOffline(); assert(stops == 1 && !toolsRunning());
  toolsTick(false); assert(starts == 1); // hard lock cannot restart saved tool
  toolsOnline(); toolsStop(); toolsTick(false); assert(starts == 1); // cancel pending startup
  rejectStart = true;
  assert(!toolsStart("usb-hotspot", error) && !toolsRunning());
  assert(toolsStatus(USB_HOTSPOT_TOOL).state == "error");
  rejectStart = false;
  assert(toolsStart("usb-hotspot", error));
  toolsTick(false); assert(ticks >= 1);
  testStorageFail = true;
  assert(!toolsSetStartup("") && toolsStartupId() == "usb-hotspot");
  testStorageFail = false;
  toolsReset(); assert(!toolsRunning() && toolsStartupId().isEmpty());
  assert(!toolsSetStartup("missing"));

  uint8_t packet[1600] = {};
  uint32_t offset, length;
  put32(packet, 1); put32(packet + 4, 58); put32(packet + 8, 36); put32(packet + 12, 14);
  assert(flea_rndis_packet(packet, 58, &offset, &length) && offset == 44 && length == 14);
  assert(!flea_rndis_packet(packet, 43, &offset, &length));
  put32(packet + 4, 44); assert(!flea_rndis_packet(packet, 58, &offset, &length));
  put32(packet + 4, 58); put32(packet + 8, 0xfffffff8);
  assert(!flea_rndis_packet(packet, 58, &offset, &length));
  put32(packet + 8, 0); assert(!flea_rndis_packet(packet, 58, &offset, &length)); // header overlap
  put32(packet + 8, 36); put32(packet + 12, 1515); put32(packet + 4, 1559);
  assert(!flea_rndis_packet(packet, 1559, &offset, &length));
  put32(packet, 5); put32(packet + 4, 32); put32(packet + 12, 0x0001010e);
  put32(packet + 16, 4); put32(packet + 20, 20);
  assert(flea_rndis_control_capacity(true, 1024, 256)); // Windows response request
  assert(flea_rndis_control_capacity(true, 65535, 256)); // host receive capacity
  assert(flea_rndis_control_capacity(false, 256, 256));
  assert(!flea_rndis_control_capacity(false, 257, 256)); // bound incoming commands
  assert(!flea_rndis_control_capacity(false, 1024, 256));
  assert(flea_rndis_control(packet, 32, 256));
  assert(!flea_rndis_control(packet, 31, 256));
  put32(packet + 20, 0xfffffffc); assert(!flea_rndis_control(packet, 32, 256));
  put32(packet + 20, 20); put32(packet + 16, 0);
  assert(!flea_rndis_control(packet, 32, 256)); // filter handler reads 4 bytes
  put32(packet, 2); put32(packet + 4, 8); assert(!flea_rndis_control(packet, 8, 256));
  // Exercise every truncation of a valid packet; no fields may be read past its header.
  put32(packet, 1); put32(packet + 4, 58); put32(packet + 8, 36); put32(packet + 12, 14);
  for (unsigned n = 0; n < 58; ++n) assert(!flea_rndis_packet(packet, n, &offset, &length));
  uint8_t dns[] = {0x12,0x34,1,0,0,1,0,0,0,0,0,1, 1,'a',0,0,1,0,1,
                   0,0,41,0x10,0,0,0,0,0,0,0};
  assert(hotspotDnsPrepare(dns, sizeof(dns)));
  assert(dnsWord(dns + 22) == 1232); // EDNS clamped, ID and question intact
  assert(dns[0] == 0x12 && dns[1] == 0x34 && dns[13] == 'a');
  for (size_t n = 0; n < sizeof(dns); ++n) assert(!hotspotDnsPrepare(dns, n));
  dns[12] = 0xff; assert(!hotspotDnsPrepare(dns, sizeof(dns))); // bad pointer
  dns[12] = 64; assert(!hotspotDnsPrepare(dns, sizeof(dns))); // reserved label
  dns[12] = 1; dns[2] = 0x81; assert(!hotspotDnsPrepare(dns, sizeof(dns))); // response
  assert(!hotspotSubnetsOverlap(0xc0a80401, 0xffffff00, 0xc0a88902, 0xffffff00));
  assert(hotspotSubnetsOverlap(0xc0a80401, 0xffffff00, 0xc0a80402, 0xffffff00));
  assert(hotspotSubnetsOverlap(0xc0a80401, 0xffffff00, 0xc0a88902, 0xffff0000));
  assert(hotspotSubnetsOverlap(0xc0a80401, 0xffff0000, 0xc0a88902, 0xffffff00));
  assert(hotspotSubnetsOverlap(0xc0a80401, 0, 0xc0a88902, 0xffffff00));
  puts("PASS: plugin lifecycle, startup/lock precedence, persistence failures, RNDIS/DNS bounds");
}
