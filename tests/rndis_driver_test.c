#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../fleabyte/src/usb_rndis/rndis_config.h"
#include "../fleabyte/src/usb_rndis/rndis_api.h"
#include "../fleabyte/src/usb_rndis/rndis_protocol.h"

uint8_t flea_rndis_mac_address[6] = {2,0,0,0,0,1};
static bool busy[256], reject;
static uint16_t submitted[256], filter;
static uint8_t *buffers[256];
static int completed;
static uint16_t responseLength;
static uint8_t response[256];
static void *controlBuffer;
static int successful, failed;
static int resets;
void hotspotDiagnosticLog(const char *format, ...) { (void)format; }

bool usbd_edpt_open(uint8_t port, const tusb_desc_endpoint_t *ep) { (void)port; busy[ep->bEndpointAddress] = false; return true; }
bool usbd_open_edpt_pair(uint8_t port, const uint8_t *p, uint8_t count, uint8_t type, uint8_t *out, uint8_t *in) {
  (void)port; (void)type; assert(count == 2);
  *in = p[2]; *out = p[9]; return true;
}
bool usbd_edpt_claim(uint8_t port, uint8_t ep) { (void)port; return !busy[ep]; }
bool usbd_edpt_busy(uint8_t port, uint8_t ep) { (void)port; return busy[ep]; }
bool usbd_edpt_xfer(uint8_t port, uint8_t ep, uint8_t *buffer, uint16_t size, bool isr) {
  (void)port; (void)isr;
  if (reject || busy[ep]) return false;
  busy[ep] = true; submitted[ep] = size; buffers[ep] = buffer; return true;
}
bool tud_control_xfer(uint8_t port, const tusb_control_request_t *request, void *data, uint16_t size) {
  (void)port; responseLength = tu_min16(size, request->wLength);
  controlBuffer = data;
  if (data && responseLength) memcpy(response, data, responseLength);
  return true;
}
bool tud_control_status(uint8_t port, const tusb_control_request_t *request) { (void)port; (void)request; return true; }
tusb_speed_t tud_speed_get(void) { return TUSB_SPEED_FULL; }
void flea_rndis_filter_cb(uint16_t value) { filter = value; }
bool flea_rndis_recv_cb(const uint8_t *data, uint16_t size) { (void)data; (void)size; return false; }
uint16_t flea_rndis_xmit_cb(uint8_t *data, void *ref, uint16_t size) { memcpy(data, ref, size); return size; }
void flea_rndis_xmit_done_cb(void) { ++completed; }
void flea_rndis_xmit_result_cb(bool success) { if (success) ++successful; else ++failed; }
void flea_rndis_reset_cb(void) { ++resets; }
static void finish(uint8_t ep, xfer_result_t result) {
  uint16_t size = submitted[ep]; assert(busy[ep]); busy[ep] = false;
  assert(flea_rndis_xfer_cb(0, ep, result, size));
}
static void open_network(void) {
  static const uint8_t descriptor[] = {TUD_RNDIS_DESCRIPTOR(0, 0, 0x81, 64, 0x02, 0x82, 64)};
  assert(flea_rndis_open(0, (const tusb_desc_interface_t *)(descriptor + 8), sizeof(descriptor)-8));
  assert(flea_rndis_can_xmit(1514));
}
int main(void) {
  flea_rndis_init(); open_network();
  // Rejected RX renewal must be retried; a duplicate renewal must never submit
  // onto an already armed endpoint or overwrite a held receive buffer.
  assert(busy[0x02]);
  flea_rndis_recv_renew(); assert(busy[0x02]);
  reject = true;
  finish(0x02, XFER_RESULT_FAILED); assert(!busy[0x02]);
  reject = false;
  flea_rndis_service(); assert(busy[0x02]);
  uint8_t frame[1514] = {0};
  for (unsigned i = 0; i < sizeof(frame); ++i) frame[i] = (uint8_t)i;
  for (uint16_t size = 14; size <= sizeof(frame); ++size) {
    int before = completed;
    assert(flea_rndis_try_xmit(frame, size)); assert(!flea_rndis_can_xmit(size));
    uint8_t message[1602]; unsigned length = 0;
    const unsigned expected = size + 44 + ((size + 44) % 64 == 0 ? 1 : 0);
    while (busy[0x82]) {
      unsigned count = submitted[0x82];
      assert(count > 0 && count <= 64 && length + count <= expected);
      assert(count == (expected - length >= 64 ? 64 : expected - length));
      memcpy(message + length, buffers[0x82], count); length += count;
      assert(!flea_rndis_can_xmit(size) && completed == before);
      finish(0x82, XFER_RESULT_SUCCESS);
      if (length < expected) assert(busy[0x82] && completed == before);
      else assert(count < 64); // only the final USB packet terminates the host transfer
    }
    const rndis_data_packet_t *packet = (const rndis_data_packet_t *)message;
    assert(packet->MessageLength == (uint32_t)size + 44 && packet->DataLength == size);
    assert(length == expected && memcmp(message + 44, frame, size) == 0);
    if ((size + 44) % 64 == 0) assert(message[length - 1] == 0);
    assert(flea_rndis_can_xmit(size) && completed == before + 1);
  }
  flea_rndis_xmit(frame, 128); finish(0x82, XFER_RESULT_SUCCESS); finish(0x82, XFER_RESULT_FAILED);
  assert(failed == 1 && successful == 1501);
  assert(flea_rndis_can_xmit(128));
  reject = true; assert(!flea_rndis_try_xmit(frame, 128)); reject = false;
  assert(flea_rndis_can_xmit(128));
  // A rejected continuation must keep message ownership and retry the same
  // offset, never report a partially sent Ethernet frame as completed.
  int beforeRetry = completed;
  assert(flea_rndis_try_xmit(frame, 128));
  reject = true; finish(0x82, XFER_RESULT_SUCCESS);
  assert(!busy[0x82] && !flea_rndis_can_xmit(14) && completed == beforeRetry);
  flea_rndis_service(); assert(!busy[0x82]);
  reject = false; flea_rndis_service();
  assert(busy[0x82] && submitted[0x82] == 64 && memcmp(buffers[0x82], frame + 20, 64) == 0);
  while (busy[0x82]) finish(0x82, XFER_RESULT_SUCCESS);
  assert(completed == beforeRetry + 1 && flea_rndis_can_xmit(14));
  // A successful controller result with fewer bytes than submitted must not
  // advance the message offset or report the Ethernet frame as delivered.
  int beforeShort = completed, successBeforeShort = successful, failureBeforeShort = failed;
  assert(flea_rndis_try_xmit(frame, 128));
  busy[0x82] = false;
  assert(flea_rndis_xfer_cb(0, 0x82, XFER_RESULT_SUCCESS, 63));
  assert(!busy[0x82] && flea_rndis_can_xmit(14));
  assert(completed == beforeShort + 1 && successful == successBeforeShort && failed == failureBeforeShort + 1);
  flea_rndis_service(); assert(!busy[0x82]);
  for (int cycle = 0; cycle < 10; ++cycle) {
    memset(busy, 0, sizeof(busy)); // controller resets endpoint state first
    flea_rndis_reset(0); assert(filter == 0 && !flea_rndis_can_xmit(14));
    open_network();
    uint32_t initialize[6] = {2,24,1,1,0,16384};
    tusb_control_request_t request = {.bmRequestType=0x21,.bRequest=0,.wLength=24};
    assert(flea_rndis_control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
    memcpy(controlBuffer, initialize, sizeof(initialize));
    assert(flea_rndis_control_xfer_cb(0, CONTROL_STAGE_DATA, &request));
    finish(0x81, XFER_RESULT_SUCCESS);
    request.bmRequestType = 0xa1; request.bRequest = 1; request.wLength = 1024;
    assert(flea_rndis_control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
    assert(responseLength == sizeof(rndis_initialize_cmplt_t));
    assert(((rndis_initialize_cmplt_t *)response)->MessageType == REMOTE_NDIS_INITIALIZE_CMPLT);
    assert(((rndis_initialize_cmplt_t *)response)->Status == RNDIS_STATUS_SUCCESS);
    uint32_t setFilter[8] = {5,32,2,0x0001010e,4,20,0,15};
    request.bmRequestType = 0x21; request.bRequest = 0; request.wLength = sizeof(setFilter);
    assert(flea_rndis_control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
    memcpy(controlBuffer, setFilter, sizeof(setFilter));
    assert(flea_rndis_control_xfer_cb(0, CONTROL_STAGE_DATA, &request));
    assert(filter == 15); finish(0x81, XFER_RESULT_SUCCESS);
    assert(flea_rndis_try_xmit(frame, 128));
    finish(0x82, XFER_RESULT_SUCCESS); // deinit while the second chunk is active
    assert(flea_rndis_deinit()); assert(filter == 0 && !flea_rndis_can_xmit(14));
  }
  puts("PASS: production RNDIS packet chaining, every Ethernet frame size, message ownership, failures, retry and resets");
  assert(resets == 21); // initial init plus ten reset/deinit cycles
}
