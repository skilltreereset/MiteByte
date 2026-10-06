#include "usb_hotspot.h"
#include "hotspot_dns.h"
#include "../../diagnostics/diag_log.h"
#include "net_rules.h"
#include "usb_rx_backpressure.h"
#include "usb_rx_buffers.h"
#include "usb_tx_scratch.h"
#include "../../../usb_mode.h"
#include "../../../web_api.h"
#include "../../../macro.h"
#include "../../usb_ethernet/rndis_api.h"
#include "../../../generated/windows_setup.h"
#include "USB.h"
#include <WiFi.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <esp_mac.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>
#include <cstring>

// ---------------------------------------------------------------------------
// RAM & CPU optimisation backlog (T-Dongle-S3, no PSRAM; USB Full-Speed ~1 MB/s)
// Free internal heap runs ~45-60 KB and dips under download+DNS bursts. Logging
// is already compile-gated (HOTSPOT_DIAGNOSTICS, off by default). Remaining levers:
//
//   1. Right-size task stacks      ~8-12 KB  safe after high-water check
//      (sdwatch 8K, macro 8K, raTask 4K look oversized).
//   2. Framework sdkconfig rebuild ~40-70 KB hard; the ROOT (WiFi/lwIP burst).
//   3. Phase D: RX single-copy     (CPU, not RAM) parked; RAM is the limiter.
//
// #2 in detail -- what it takes:
//   The big RAM knobs are compiled INTO pioarduino's prebuilt libs, so build_flags
//   cannot change them. Build arduino-esp32 FROM SOURCE with a custom sdkconfig
//   (pioarduino can, via sdkconfig.defaults + framework source build; first build
//   is much slower). You do NOT ship libraries to users -- the knobs bake into
//   firmware.bin, so anyone flashing the released .bin gets them; only your build
//   tree carries the sdkconfig. Knobs (USB-capped, so ~zero throughput cost):
//     CONFIG_ESP_WIFI_AMPDU_TX/RX_ENABLED = n        ~15-30 KB (reorder buffers)
//     ESP_WIFI_DYNAMIC_RX/TX_BUFFER_NUM 32 -> 8      ~20-40 KB peak (burst)
//     CONFIG_LWIP_TCP_WND / TCP_SND_BUF  5744 -> 2880 ~3-6 KB per connection
//     CONFIG_TINYUSB NCM off (we use RNDIS)          ~6.4 KB static (ncm_epbuf)
//     TinyUSB host off (device-only)                 ~4 KB static (hidh/usbh)
//   Estimate: burst heap floor rises from today's ~24 KB clamp toward ~40-60 KB,
//   and tx_full drops fall sharply. Estimates until measured on hardware.
// ---------------------------------------------------------------------------

#ifndef USB_VID
#define USB_VID USB_ESPRESSIF_VID
#endif
#ifndef USB_PID
#define USB_PID 0x0002
#endif

// Microsoft class matching permits inbox RNDIS on Windows 10 and 11.
#undef TUD_RNDIS_ITF_CLASS
#undef TUD_RNDIS_ITF_SUBCLASS
#undef TUD_RNDIS_ITF_PROTOCOL
#define TUD_RNDIS_ITF_CLASS 0xEF
#define TUD_RNDIS_ITF_SUBCLASS 0x04
#define TUD_RNDIS_ITF_PROTOCOL 0x01
static const uint8_t s_hotspotConfig[] = {
  TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + TUD_RNDIS_DESC_LEN, 0, 500),
  TUD_RNDIS_DESCRIPTOR(0, 0, 0x81, 64, 0x02, 0x82, 64)
};
static const tusb_desc_device_t s_hotspotDevice = {
  sizeof(tusb_desc_device_t), TUSB_DESC_DEVICE, 0x0200,
  TUSB_CLASS_MISC, MISC_SUBCLASS_COMMON, MISC_PROTOCOL_IAD, CFG_TUD_ENDPOINT0_SIZE,
  USB_VID, USB_PID, 0x0100, 1, 2, 3, 1
};
static const UsbToolProfile s_profile = {
  s_hotspotConfig, reinterpret_cast<const uint8_t *>(&s_hotspotDevice),
  "MiteByte USB Hotspot", 'H'
};

extern "C" { uint8_t flea_rndis_mac_address[6] = {}; }
using Frame = HotspotFrame;

// Kernel objects and helpers, created lazily in start(); never atomics.
static QueueHandle_t s_rx = nullptr, s_tx = nullptr;
static TaskHandle_t s_rxTask = nullptr;
static SemaphoreHandle_t s_ioLock = nullptr;
static UsbRxBackpressure s_rxBackpressure;
static UsbRxBuffers s_rxBuffers;
static UsbTxScratch s_txScratch;
static esp_netif_t *s_usb = nullptr, *s_ap = nullptr, *s_previousDefault = nullptr;

// Diagnostic counters only. Written from several tasks, read for the SD log and
// the status panel; atomic so those reads never tear, skew between them is fine.
struct Counters {
  std::atomic<uint32_t> received{0}, sent{0}, dropped{0};
  std::atomic<uint32_t> txFull{0}, rxErrors{0};
  std::atomic<int> lastRxError{0};
  std::atomic<uint32_t> usbErrors{0}, submitted{0};
  std::atomic<uint32_t> rxPauses{0}, usbResets{0};
  std::atomic<uint32_t> inStarted{0};        // millis() of the in-flight IN, 0 if none
  std::atomic<unsigned> wifiDisconnectReason{0};
  std::atomic<uint32_t> intervalMinHeap{UINT32_MAX}; // lowest free heap this diagnostic window
};
static Counters s_io;

// Lifecycle and data-flow flags shared across the USB, receive-worker and UI
// tasks. All atomic because every one crosses a task boundary.
struct Link {
  std::atomic<bool> running{false}, filter{false}, linkReady{false};
  std::atomic<bool> memoryPaused{false}, diagnosticQueued{false};
  std::atomic<bool> rxPumpQueued{false}, pumpQueued{false};
  std::atomic<uint32_t> generation{0};       // bumped on (re)start and USB reset
  std::atomic<uint32_t> txWriters{0};        // producers inside transmit(), drained by stop()
};
static Link s_link;

// Plain state touched only from a single context (the UI loop's tick(), or the
// receive worker). Not atomic by design; see each field's owning comment.
struct Local {
  uint32_t seenUsbReset = 0;     // tick(): last observed s_io.usbResets
  uint32_t nextDiagnostic = 0;   // tick(): next 2 s diagnostic dump
  uint32_t nextAddressPoll = 0;  // tick(): throttles the lwIP address/NAPT poll
  size_t lastLargestBlock = 0;   // receiveWorker: cached fragmentation reading
  uint32_t lastLargestBlockAt = 0;
  uint32_t lastDnsServiceAt = 0; // receiveWorker: last DNS/address service time
  bool connected = false;        // tick(): USB filter+mount edge-detected
  bool napt = false;             // tick(): forwarding currently enabled
  esp_netif_ip_info_t lastAddress = {};
  String error;
};
static Local s_state;

static void transmitPump(void *);
static void scheduleTransmit() {
  if (s_link.running.load() && s_link.filter.load() && !s_link.pumpQueued.exchange(true))
    usbd_defer_func(transmitPump, nullptr, false);
}

extern "C" void flea_rndis_filter_cb(uint16_t filter) {
  hotspotDiagnosticLog("RNDIS_FILTER value=0x%04x mounted=%d", filter, tud_mounted());
  s_link.filter = (filter != 0);
  if (!filter && s_rxBackpressure.waiting()) {
    s_rxBackpressure.cancel(); flea_rndis_recv_renew();
  }
}
extern "C" void flea_rndis_reset_cb(void) {
  hotspotDiagnosticLog("USB_CLASS_RESET generation=%u", s_link.generation.load());
  // Runs before filter_cb(0); no OUT renewal is legal on a reset endpoint.
  s_rxBackpressure.cancel(); s_link.rxPumpQueued = false; s_link.pumpQueued = false;
  s_io.inStarted = 0;
  ++s_link.generation; ++s_io.usbResets;
}
static bool enqueueReceived(const Frame &frame) {
  if (xQueueSend(s_rx, &frame, 0) != pdTRUE) return false;
  ++s_io.received; return true;
}
extern "C" bool flea_rndis_recv_cb(const uint8_t *data, uint16_t size) {
  if (!s_link.running.load() || !s_rx || size < 14 || size > 1514) return false;
  HOTSPOT_VPACKET("USB_RX", data, size);
  return s_rxBackpressure.accept(data, size, enqueueReceived, flea_rndis_recv_renew, s_link.generation.load());
}
static void receivePump(void *) {
  s_link.rxPumpQueued = false;
  if (s_link.running.load()) s_rxBackpressure.retry(enqueueReceived, flea_rndis_recv_renew);
  else s_rxBackpressure.cancel();
}
static void scheduleReceiveRetry() {
  // USB interrupt events share this queue. Scheduling for every packet can
  // crowd out transfer completions and even enumeration/reset events.
  if (s_rxBackpressure.waiting() && !s_link.rxPumpQueued.exchange(true))
    usbd_defer_func(receivePump, nullptr, false);
}
// receiveWorker only. heap_caps_get_largest_free_block walks the whole free
// list; heap_caps_get_free_size is a cheap running counter. Skip the walk when
// the pool is full (nothing to hand out anyway) and when memory is comfortable
// and a recent reading was healthy, reusing a value at most 10 ms old. Near the
// reserve, or after a low reading, it refreshes every frame as before.
// Track the lowest free heap seen within the current diagnostic window so the
// log shows which 2 s window dipped, not just the sticky all-time minimum.
static void noteHeap(uint32_t freeHeap) {
  uint32_t cur = s_io.intervalMinHeap.load(std::memory_order_relaxed);
  while (freeHeap < cur &&
         !s_io.intervalMinHeap.compare_exchange_weak(cur, freeHeap, std::memory_order_relaxed)) {}
}
static void *acquireReceiveBuffer() {
  if (s_rxBuffers.full()) return nullptr;
  size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  noteHeap(freeHeap);
  uint32_t now = millis();
  bool tight = freeHeap < UsbRxBuffers::ComfortableHeap ||
               s_state.lastLargestBlock < UsbRxBuffers::AllocationReserve;
  if (tight || int32_t(now - s_state.lastLargestBlockAt) >= 10) {
    s_state.lastLargestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_state.lastLargestBlockAt = now;
  }
  return s_rxBuffers.acquire(freeHeap, s_state.lastLargestBlock);
}
static void receiveWorker(void *) {
  // This worker is created once and is the frame's only owner. Socket calls
  // below can synchronously enter lwIP output; do not keep 1.5 KiB on stack.
  static Frame frame;
  bool received = false;
  for (;;) {
    if (!received) {
      // Idle longer when the DNS proxy has nothing in flight; any queued frame
      // (a new DNS query included) wakes us immediately regardless of this wait.
      TickType_t wait = hotspotDnsBusy() ? (pdMS_TO_TICKS(2) + 1) : pdMS_TO_TICKS(10);
      received = xQueueReceive(s_rx, &frame, wait) == pdTRUE;
    }
    // The first host packet can arrive before the main loop raises the netif.
    while (received && frame.generation == s_link.generation.load() && s_link.running.load() && !s_link.linkReady.load()) vTaskDelay(1);
    xSemaphoreTake(s_ioLock, portMAX_DELAY);
    if (s_link.running.load()) {
      if (received && frame.generation == s_link.generation.load() && s_link.linkReady.load()) {
        void *copy = acquireReceiveBuffer();
        if (!copy) {
          // Keep this frame and let the bounded RX queue pause USB OUT. Do
          // not discard it or allocate more payloads while lwIP is congested.
          if (!s_link.memoryPaused.exchange(true)) ++s_io.rxPauses;
        } else {
          s_link.memoryPaused = false;
          memcpy(copy, frame.bytes, frame.size);
          // esp-netif owns and frees this buffer on both success and failure.
          esp_err_t result = esp_netif_receive(s_usb, copy, frame.size, nullptr);
          received = false;
          scheduleReceiveRetry();
          if (result != ESP_OK) {
            ++s_io.dropped; ++s_io.rxErrors; s_io.lastRxError = result;
            hotspotDiagnosticLog("NETIF_RECEIVE_ERROR result=%d bytes=%u", result, frame.size);
          }
        }
      } else { received = false; s_link.memoryPaused = false; }
      // DNS must keep moving even while the web server parses or sends a page.
      // These address lookups marshal to the lwIP task (a blocking IPC each), so
      // do not run them for every forwarded frame. 2 ms keeps DNS responsive
      // under a saturating transfer; idle wakeups already pace at ~2 ms.
      uint32_t now = millis();
      if (int32_t(now - s_state.lastDnsServiceAt) >= 2) {
        s_state.lastDnsServiceAt = now;
        noteHeap(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)); // sample TX-only/idle windows too
        esp_netif_ip_info_t ip = {};
        if (s_link.linkReady.load()) esp_netif_get_ip_info(s_usb, &ip);
        esp_netif_dns_info_t dns = {};
        if (ip.ip.addr && ip.gw.addr) esp_netif_get_dns_info(s_usb, ESP_NETIF_DNS_MAIN, &dns);
        hotspotDnsTick(IPAddress(ip.ip.addr && ip.gw.addr ?
                                (dns.ip.u_addr.ip4.addr ? dns.ip.u_addr.ip4.addr : ip.gw.addr) : 0));
      }
    } else { received = false; s_link.memoryPaused = false; }
    xSemaphoreGive(s_ioLock);
    // Still holding a frame: wait for lwIP to free a buffer (freeReceived
    // notifies), falling back after ~1 ms for the memory-tight case.
    static constexpr TickType_t kPausedWait = pdMS_TO_TICKS(1) > 0 ? pdMS_TO_TICKS(1) : 1;
    if (received) ulTaskNotifyTake(pdTRUE, kPausedWait);
  }
}
extern "C" uint16_t flea_rndis_xmit_cb(uint8_t *data, void *ref, uint16_t size) {
  memcpy(data, ref, size); return size;
}
static esp_err_t transmit(void *, void *buffer, size_t size) {
  struct Writer {
    Writer() { ++s_link.txWriters; }
    ~Writer() { --s_link.txWriters; }
  } writer;
  if (!s_link.running.load() || !s_link.filter.load() || !s_tx || size > 1514 || size < 14)
    return ESP_ERR_INVALID_STATE;
  if (!s_txScratch.enqueue(s_tx, buffer, size, s_link.generation.load())) {
    ++s_io.dropped; ++s_io.txFull; return ESP_ERR_NO_MEM;
  }
  HOTSPOT_VPACKET("USB_TX", static_cast<const uint8_t *>(buffer), size);
  scheduleTransmit(); // wake USB at production time, independently of the UI loop
  return ESP_OK;
}
static void freeReceived(void *, void *buffer) {
  s_rxBuffers.release(buffer);
  // Wake the receive worker the instant a slot frees, instead of letting it
  // sleep out its 1 ms retry. Only relevant while it is paused holding a frame.
  // Runs in lwIP task context (pbuf free), never an ISR, so the plain give is
  // correct. A memory-tight pause has no free signal and relies on the timeout.
  if (s_rxTask && s_link.memoryPaused.load()) xTaskNotifyGive(s_rxTask);
}
static void transmitOne() {
  if (s_link.running.load() && s_link.filter.load() && tud_mounted() && flea_rndis_can_xmit(1514)) {
    static Frame frame;
    if (xQueuePeek(s_tx, &frame, 0) == pdTRUE) {
      if (frame.generation != s_link.generation.load()) { xQueueReceive(s_tx, &frame, 0); return; }
      if (flea_rndis_try_xmit(frame.bytes, frame.size)) {
        s_io.inStarted = millis();
        xQueueReceive(s_tx, &frame, 0); ++s_io.submitted;
      } else ++s_io.usbErrors; // retain the queued frame if submission was rejected
    }
  }
}
static void transmitPump(void *) { s_link.pumpQueued.store(false); transmitOne(); }
extern "C" void flea_rndis_xmit_done_cb(void) { transmitOne(); }
extern "C" void flea_rndis_xmit_result_cb(bool success) {
  s_io.inStarted = 0;
  if (success) ++s_io.sent;
  else { ++s_io.usbErrors; ++s_io.dropped; hotspotDiagnosticLog("USB_TX_COMPLETION_FAILED"); }
}
static void transportDiagnostic(void *) {
  s_link.diagnosticQueued = false;
  if (s_link.running.load()) flea_rndis_service();
}
// Write the station MAC into the TinyUSB-visible array. Call once in begin(),
// before the host enumerates: TinyUSB reads flea_rndis_mac_address directly
// during enumeration, so it must be final by then. Locally administered, unicast.
static void setStationMac() {
  esp_efuse_mac_get_default(flea_rndis_mac_address);
  flea_rndis_mac_address[0] = (flea_rndis_mac_address[0] | 2) & 0xfe;
  flea_rndis_mac_address[5] ^= 0x48;
}
static void begin() {
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
    hotspotDiagnosticLog("WIFI_CLIENT_CONNECTED");
  }, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    hotspotDiagnosticLog("WIFI_CLIENT_ADDRESS ip=%s", IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
  }, ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    s_io.wifiDisconnectReason = info.wifi_ap_stadisconnected.reason;
    hotspotDiagnosticLog("WIFI_DISCONNECT reason=%u", info.wifi_ap_stadisconnected.reason);
  }, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
  setStationMac();
}
static bool start(String &error) {
  MacroStatus macro = macroGetStatus();
  if (macro.state == MACRO_RUNNING || macro.state == MACRO_ARMED) {
    error = "Stop the script first"; return false;
  }
  // Four owned lwIP buffers + two waiting RX frames, rather than allocating
  // each arriving payload. Smaller queues leave about 18 KiB more for Wi-Fi,
  // NAPT, DNS and web sockets than the former twelve-frame queues.
  if (!s_rx) s_rx = xQueueCreate(2, sizeof(Frame));
  if (!s_tx) s_tx = xQueueCreate(6, sizeof(Frame));
  if (!s_rx || !s_tx) { error = "Not enough memory for sharing"; return false; }
  if (!s_txScratch.begin()) { error = "Could not create USB transmit lock"; return false; }
  if (!s_ioLock) s_ioLock = xSemaphoreCreateMutex();
  if (!s_ioLock) { error = "Could not create sharing lock"; return false; }
  if (!s_rxTask && xTaskCreate(receiveWorker, "usb-net-rx", 4608, nullptr, 3, &s_rxTask) != pdPASS) {
    error = "Could not start USB receive task"; return false;
  }
  xQueueReset(s_rx); xQueueReset(s_tx);
  ++s_link.generation;
  s_state.error = ""; s_io.received = 0; s_io.sent = 0; s_io.dropped = 0;
  s_io.txFull = 0; s_io.rxErrors = 0; s_io.lastRxError = 0; s_link.linkReady = false;
  s_io.usbErrors = 0; s_io.submitted = 0;
  s_io.rxPauses = 0; s_link.memoryPaused = false; s_io.inStarted = 0; s_link.diagnosticQueued = false;
  s_link.rxPumpQueued = false; s_link.pumpQueued = false;
  s_state.lastLargestBlock = 0; s_state.lastLargestBlockAt = 0; s_state.lastDnsServiceAt = 0;
  s_state.nextAddressPoll = 0;
  s_io.intervalMinHeap = UINT32_MAX;
  s_ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (!s_ap) { error = "Wi-Fi hotspot is unavailable"; return false; }
  if (!s_usb) {
    esp_netif_inherent_config_t base = {};
    base.flags = ESP_NETIF_DHCP_CLIENT;
    base.get_ip_event = IP_EVENT_ETH_GOT_IP;
    base.lost_ip_event = IP_EVENT_ETH_LOST_IP;
    base.if_key = "MITEBYTE_USB"; base.if_desc = "USB internet"; base.route_prio = 100;
    esp_netif_driver_ifconfig_t driver = {};
    // esp_netif treats a null driver handle as "no driver attached" and skips
    // transmit/free callbacks; any non-null token works since we hold no driver.
    static void *const kNetifDriverHandle = reinterpret_cast<void *>(1);
    driver.handle = kNetifDriverHandle; driver.transmit = transmit;
    driver.driver_free_rx_buffer = freeReceived;
    esp_netif_config_t config = { &base, &driver, ESP_NETIF_NETSTACK_DEFAULT_ETH };
    s_usb = esp_netif_new(&config);
    uint8_t mac[6]; memcpy(mac, flea_rndis_mac_address, 6); mac[5] ^= 1;
    if (!s_usb || esp_netif_set_mac(s_usb, mac) != ESP_OK) {
      if (s_usb) esp_netif_destroy(s_usb);
      s_usb = nullptr; error = "Could not create USB network"; return false;
    }
  }
  hotspotDiagnosticsBegin();
  esp_netif_dhcp_status_t apDhcp = ESP_NETIF_DHCP_INIT;
  esp_err_t apResult = esp_netif_dhcps_get_status(s_ap, &apDhcp);
  if (apResult == ESP_OK && apDhcp != ESP_NETIF_DHCP_STARTED) {
    apResult = esp_netif_dhcps_start(s_ap);
    if (apResult == ESP_OK) apResult = esp_netif_dhcps_get_status(s_ap, &apDhcp);
  }
  hotspotDiagnosticLog("AP_DHCP status=%d result=%d", int(apDhcp), int(apResult));
  if (apResult != ESP_OK || apDhcp != ESP_NETIF_DHCP_STARTED) {
    error = "Wi-Fi address server is unavailable";
    hotspotDiagnosticsEnd(); return false;
  }
  s_state.nextDiagnostic = 0;
  hotspotDiagnosticLog("START AP=%s USB_host_mac=%02x:%02x:%02x:%02x:%02x:%02x", WiFi.softAPIP().toString().c_str(),
    flea_rndis_mac_address[0],flea_rndis_mac_address[1],flea_rndis_mac_address[2],flea_rndis_mac_address[3],flea_rndis_mac_address[4],flea_rndis_mac_address[5]);
  webSetCaptiveDns(false);
  xSemaphoreTake(s_ioLock, portMAX_DELAY);
  if (!hotspotDnsBegin()) {
    error = hotspotDnsError();
    macroLog(error);
    hotspotDiagnosticLog("START_FAILED %s", error.c_str());
    xSemaphoreGive(s_ioLock); webSetCaptiveDns(true); hotspotDiagnosticsEnd(); return false;
  }
  s_link.filter = false; s_state.connected = false; s_state.napt = false;
  s_state.lastAddress = {};
  s_previousDefault = esp_netif_get_default_netif();
  esp_netif_action_start(s_usb, nullptr, 0, nullptr);
  if (!usbModeSetToolProfile(&s_profile)) {
    esp_netif_action_stop(s_usb, nullptr, 0, nullptr);
    hotspotDnsStop(); xSemaphoreGive(s_ioLock);
    // Revert to the prior storage/HID profile: restores descriptors, re-inits
    // the controller and the normal USB task priority. Without this a failed
    // switch leaves USB dead (s_active stays false, so stop() is never called).
    usbModeSetToolProfile(nullptr);
    webSetCaptiveDns(true);
    error = "Could not restart USB controller";
    hotspotDiagnosticLog("START_FAILED USB restart"); hotspotDiagnosticsEnd(); return false;
  }
  s_link.running = true;
  hotspotDiagnosticLog("STARTED waiting_for_host=1");
  xSemaphoreGive(s_ioLock);
  macroLog("== Wi-Fi hotspot started; waiting for PC sharing ==");
  return true;
}
static void stop() {
  hotspotDiagnosticLog("STOP requested");
  xSemaphoreTake(s_ioLock, portMAX_DELAY);
  s_link.running = false;
  ++s_link.generation;
  while (s_link.txWriters.load()) vTaskDelay(1); // quiesce producers before deleting the USB event queue
  s_link.linkReady = false;
  if (s_state.napt) esp_netif_napt_disable(s_ap);
  s_state.napt = false; s_state.connected = false;
  if (s_usb) {
    esp_netif_action_disconnected(s_usb, nullptr, 0, nullptr);
    esp_netif_action_stop(s_usb, nullptr, 0, nullptr);
  }
  if (s_previousDefault) esp_netif_set_default_netif(s_previousDefault);
  hotspotDnsStop(); webSetCaptiveDns(true);
  usbModeSetToolProfile(nullptr);
  xQueueReset(s_rx); xQueueReset(s_tx);
  xSemaphoreGive(s_ioLock);
  macroLog("== Wi-Fi hotspot stopped ==");
  hotspotDiagnosticsEnd();
}
static esp_netif_ip_info_t usbAddress() {
  esp_netif_ip_info_t ip = {};
  if (s_usb && s_state.connected) esp_netif_get_ip_info(s_usb, &ip);
  return ip;
}
static void tick() {
  bool connected = s_link.filter.load() && tud_mounted();
  bool reset = s_state.seenUsbReset != s_io.usbResets.load();
  s_state.seenUsbReset = s_io.usbResets.load();
  if (reset && s_state.connected) {
    // Do not miss a fast reset + reinitialization between two UI loop ticks.
    s_link.linkReady = false; s_state.connected = false;
    s_state.error = "";
    if (s_state.napt) esp_netif_napt_disable(s_ap);
    s_state.napt = false;
    if (s_previousDefault) esp_netif_set_default_netif(s_previousDefault);
    esp_netif_action_disconnected(s_usb, nullptr, 0, nullptr);
    xQueueReset(s_rx); xQueueReset(s_tx);
  }
  if (connected != s_state.connected) {
    hotspotDiagnosticLog("LINK connected=%d filter=%d mounted=%d", connected, s_link.filter.load(), tud_mounted());
    s_state.connected = connected;
    if (connected) {
      esp_netif_action_connected(s_usb, nullptr, 0, nullptr);
      s_link.linkReady = true;
    }
    else {
      s_link.linkReady = false;
      if (s_state.napt) esp_netif_napt_disable(s_ap);
      s_state.napt = false;
      s_state.error = "";
      if (s_previousDefault) esp_netif_set_default_netif(s_previousDefault);
      esp_netif_action_disconnected(s_usb, nullptr, 0, nullptr);
      xQueueReset(s_rx); xQueueReset(s_tx);
    }
  }
  if (connected && uxQueueMessagesWaiting(s_tx)) scheduleTransmit();
  if (connected) scheduleReceiveRetry(); // cover queue-space/hold timing races
  // Address/NAPT upkeep reads lwIP state through a blocking IPC. A free-running
  // loop would do that thousands of times a second; 20 ms is ample for bringing
  // forwarding up shortly after the host assigns an address. Reset/connection
  // events above are handled every tick, and packet flow never waits here.
  if (int32_t(millis() - s_state.nextAddressPoll) >= 0) {
    s_state.nextAddressPoll = millis() + 20;
    auto ip = usbAddress();
    if (memcmp(&ip, &s_state.lastAddress, sizeof(ip)) != 0) {
      hotspotDiagnosticLog("ADDRESS ip=%s gateway=%s mask=%s", IPAddress(ip.ip.addr).toString().c_str(),
        IPAddress(ip.gw.addr).toString().c_str(), IPAddress(ip.netmask.addr).toString().c_str());
      if (s_state.napt) esp_netif_napt_disable(s_ap);
      s_state.napt = false; s_state.error = ""; s_state.lastAddress = ip;
      if (s_previousDefault) esp_netif_set_default_netif(s_previousDefault);
    }
    bool ready = ip.ip.addr && ip.gw.addr && ip.netmask.addr;
    if (ready && !s_state.napt && s_state.error.isEmpty()) {
      esp_netif_ip_info_t ap = {};
      esp_netif_get_ip_info(s_ap, &ap);
      if (hotspotSubnetsOverlap(ip.ip.addr, ip.netmask.addr, ap.ip.addr, ap.netmask.addr)) {
        s_state.error = "PC sharing and hotspot use overlapping addresses";
      } else if (esp_netif_set_default_netif(s_usb) != ESP_OK || esp_netif_napt_enable(s_ap) != ESP_OK) {
        s_state.error = "Could not enable internet forwarding";
      } else {
        s_state.napt = true;
        hotspotDiagnosticLog("FORWARDING enabled=1");
        macroLog("== PC sharing connected ==");
      }
    }
    if (!ready && s_state.napt) { esp_netif_napt_disable(s_ap); s_state.napt = false; }
  }
  if (int32_t(millis() - s_state.nextDiagnostic) >= 0) {
    s_state.nextDiagnostic = millis() + 2000;
    auto dns = hotspotDnsStatus();
    esp_netif_dhcp_status_t dhcp = ESP_NETIF_DHCP_INIT;
    esp_netif_dhcpc_get_status(s_usb, &dhcp);
    esp_netif_dhcp_status_t apDhcp = ESP_NETIF_DHCP_INIT;
    esp_err_t apResult = esp_netif_dhcps_get_status(s_ap, &apDhcp);
    esp_netif_ip_info_t apIp = {};
    esp_netif_get_ip_info(s_ap, &apIp);
    hotspotDiagnosticLog("AP_STATE ip=%s dhcp=%d result=%d up=%d", IPAddress(apIp.ip.addr).toString().c_str(),
      int(apDhcp), int(apResult), esp_netif_is_netif_up(s_ap));
    hotspotDiagnosticLog("STATE mounted=%d suspended=%d filter=%d connected=%d dhcp=%d napt=%d clients=%u gen=%u resets=%u",
      tud_mounted(), tud_suspended(), s_link.filter.load(), s_state.connected, int(dhcp), s_state.napt,
      WiFi.softAPgetStationNum(), s_link.generation.load(), s_io.usbResets.load());
    hotspotDiagnosticLog("IO rx=%u submitted=%u completed=%u drops=%u tx_full=%u rx_errors=%u usb_errors=%u rx_queue=%u tx_queue=%u rx_held=%d",
      s_io.received.load(), s_io.submitted.load(), s_io.sent.load(), s_io.dropped.load(), s_io.txFull.load(), s_io.rxErrors.load(), s_io.usbErrors.load(),
      unsigned(uxQueueMessagesWaiting(s_rx)), unsigned(uxQueueMessagesWaiting(s_tx)), s_rxBackpressure.waiting());
    hotspotDiagnosticLog("DNS resolver=%s requests=%u forwarded=%u replies=%u timeouts=%u rejected=%u socket_error=%d",
      IPAddress(dns.resolver).toString().c_str(), dns.requests, dns.forwarded, dns.replies, dns.timeouts, dns.rejected, dns.socketError);
    uint32_t started = s_io.inStarted.load();
    hotspotDiagnosticLog("FLOW rx_owned=%u rx_paused=%d rx_pauses=%u in_age_ms=%u largest_block=%u",
      s_rxBuffers.inUse(), s_link.memoryPaused.load(), s_io.rxPauses.load(), started ? millis() - started : 0,
      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    if (!s_link.diagnosticQueued.exchange(true)) usbd_defer_func(transportDiagnostic, nullptr, false);
    uint32_t windowMin = s_io.intervalMinHeap.exchange(UINT32_MAX);
    hotspotDiagnosticLog("MEM internal=%u minimum=%u window_min=%u usb_stack=%u rx_stack=%u usb_priority=%u log_drops=%u error=%s",
      unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      windowMin == UINT32_MAX ? 0u : windowMin, usbModeTaskStackFree(),
      unsigned(uxTaskGetStackHighWaterMark(s_rxTask)), usbModeTaskPriority(), hotspotDiagnosticsDropped(), s_state.error.c_str());
  }
}
static ToolStatus status() {
  auto ip = usbAddress();
  auto dns = hotspotDnsStatus();
  bool running = s_link.running.load();
  esp_netif_dhcp_status_t apDhcp = ESP_NETIF_DHCP_INIT;
  bool apDhcpKnown = s_ap && esp_netif_dhcps_get_status(s_ap, &apDhcp) == ESP_OK;
  ToolStatus result = { running, "stopped", "Stopped", {} };
  if (running) {
    result.state = !s_state.error.isEmpty() ? "error" : s_state.napt ? "sharing" : "waiting";
    result.message = !s_state.error.isEmpty() ? s_state.error : s_state.napt ? "Sharing PC connection" :
                     !s_state.connected ? "Waiting for USB connection" : "Waiting for PC sharing";
  }
  result.details = {
    {"Wi-Fi", WiFi.softAPSSID()}, {"Connected devices", String(WiFi.softAPgetStationNum())},
    {"Diagnostic log", String(hotspotDiagnosticsState())},
    {"Diagnostic records dropped", String(hotspotDiagnosticsDropped())},
    {"Wi-Fi address server", apDhcpKnown ? (apDhcp == ESP_NETIF_DHCP_STARTED ? "Running" : "Stopped") : "Unknown"},
    {"USB address", ip.ip.addr ? IPAddress(ip.ip.addr).toString() : String("Not assigned")},
    {"DNS server", dns.resolver ? IPAddress(dns.resolver).toString() : String("Not assigned")},
    {"DNS requests / forwarded / replies", String(dns.requests) + " / " + String(dns.forwarded) + " / " + String(dns.replies)},
    {"DNS timeouts / rejected", String(dns.timeouts) + " / " + String(dns.rejected)},
    {"DNS socket error", String(dns.socketError)},
    {"Packets received / sent", String(s_io.received.load()) + " / " + String(s_io.sent.load())},
    {"Dropped packets", String(s_io.dropped.load())},
    {"Send queue full", String(s_io.txFull.load())},
    {"Receive errors", String(s_io.rxErrors.load())},
    {"Last receive error", String(s_io.lastRxError.load())},
    {"USB transfer errors", String(s_io.usbErrors.load())},
    {"USB task priority", String(usbModeTaskPriority())},
    {"USB / receive stack free", String(usbModeTaskStackFree()) + " / " + String(s_rxTask ? uxTaskGetStackHighWaterMark(s_rxTask) : 0)},
    {"Free internal memory", String(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))},
    {"Last Wi-Fi disconnect reason", String(s_io.wifiDisconnectReason.load())},
    {"USB sends submitted / completed", String(s_io.submitted.load()) + " / " + String(s_io.sent.load())}
  };
  return result;
}
const ToolPlugin USB_HOTSPOT_TOOL = {
  "usb-hotspot", "Wi-Fi Hotspot", "Share this PC's internet through the dongle's Wi-Fi.",
  begin, start, stop, tick, status,
  "Starting reconnects USB and pauses keyboard scripts and the USB drive.",
  "MiteByte-Sharing-Setup.cmd", WINDOWS_SHARING_SETUP,
  "Windows setup",
  "Run the setup script once, or download the installer for the PC. Approve Windows administrator access, then start this tool.",
  "Manual setup: open the PC internet adapter's Properties → Sharing, enable sharing, and select the dongle's USB network adapter.",
  "13-windows-hotspot-setup.txt"
};
