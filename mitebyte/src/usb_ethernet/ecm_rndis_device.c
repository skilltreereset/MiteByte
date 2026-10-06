/*
 * SPDX-FileCopyrightText: Copyright (c) 2020 Peter Lawrence
 * SPDX-FileCopyrightText: Copyright (c) 2019 Ha Thach (tinyusb.org)
 * SPDX-License-Identifier: MIT
 *
 * This file is part of the TinyUSB stack.
 */

#include "rndis_config.h"
#include "../diagnostics/diag_log.h"

#if ( CFG_TUD_ENABLED && CFG_TUD_ECM_RNDIS )

#include "device/usbd.h"
#include "device/usbd_pvt.h"

#include "class/net/net_device.h"
#include "rndis_protocol.h"
#include "rndis_validate.h"
#include "rndis_api.h"
#if CONFIG_IDF_TARGET_ESP32S3
#include <soc/usb_dwc_struct.h>
#endif

#define CFG_TUD_NET_PACKET_PREFIX_LEN sizeof(rndis_data_packet_t)
#define CFG_TUD_NET_PACKET_SUFFIX_LEN 0

#define NETD_PACKET_SIZE  (CFG_TUD_NET_PACKET_PREFIX_LEN + CFG_TUD_NET_MTU + CFG_TUD_NET_PACKET_PREFIX_LEN)
#define NETD_CONTROL_SIZE 256

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF
//--------------------------------------------------------------------+
typedef struct {
  uint8_t itf_num;      // Index number of Management Interface, +1 for Data Interface
  uint8_t itf_data_alt; // Alternate setting of Data Interface. 0 : inactive, 1 : active

  uint8_t ep_in;
  uint8_t ep_out;
  uint16_t ep_size;     // bulk endpoint max packet size (IN and OUT assumed equal)
  uint8_t ep_notif;

  bool ecm_mode;

  // Endpoint descriptor use to open/close when receiving SetInterface
  // TODO since configuration descriptor may not be long-lived memory, we should
  // keep a copy of endpoint attribute instead
  uint8_t const * ecm_desc_epdata;
} netd_interface_t;

typedef struct ecm_notify_struct {
  tusb_control_request_t header;
  uint32_t downlink, uplink;
} ecm_notify_t;

typedef struct {
  TUD_EPBUF_DEF(rx, NETD_PACKET_SIZE);
  TUD_EPBUF_DEF(tx, NETD_PACKET_SIZE);

  TUD_EPBUF_DEF(notify, sizeof(ecm_notify_t));
  TUD_EPBUF_DEF(ctrl, NETD_CONTROL_SIZE);
} netd_epbuf_t;

//--------------------------------------------------------------------+
// INTERNAL OBJECT & FUNCTION DECLARATION
//--------------------------------------------------------------------+
static netd_interface_t _netd_itf;
CFG_TUD_MEM_SECTION static netd_epbuf_t _netd_epbuf;
static bool can_xmit;
static bool rx_renew_pending;
static uint16_t in_xfer_bytes;
static uint16_t in_done_bytes, in_chunk_bytes;
static bool in_renew_pending;
static uint32_t rx_renew_errors;
static bool ecm_link_is_up = true;  // Store link state for ECM mode

//--------------------------------------------------------------------+
// Weak stubs: invoked if no strong implementation is available
//--------------------------------------------------------------------+
TU_ATTR_WEAK void tud_network_set_packet_filter_cb(uint16_t packet_filter) {
  (void) packet_filter;
}

void tud_network_recv_renew(void) {
  if (!_netd_itf.ep_out || usbd_edpt_busy(0, _netd_itf.ep_out)) return;
  rx_renew_pending = !usbd_edpt_xfer(0, _netd_itf.ep_out, _netd_epbuf.rx, NETD_PACKET_SIZE, false);
  if (rx_renew_pending) {
    ++rx_renew_errors;
    hotspotDiagnosticLog("USB_OUT_RENEW_REJECTED count=%lu", (unsigned long)rx_renew_errors);
  }
}

static bool submit_rndis_chunk(void) {
  const uint16_t count = tu_min16(in_xfer_bytes - in_done_bytes, _netd_itf.ep_size);
  in_chunk_bytes = count;
  in_renew_pending = !usbd_edpt_xfer(0, _netd_itf.ep_in, _netd_epbuf.tx + in_done_bytes, count, false);
  if (in_renew_pending) in_chunk_bytes = 0;
  return !in_renew_pending;
}

static bool do_in_xfer(uint8_t *buf, uint16_t len) {
  can_xmit = false;
  in_xfer_bytes = len; in_done_bytes = 0; in_chunk_bytes = 0; in_renew_pending = false;
  // Each controller request contains one USB packet. Full packets continue
  // the same host bulk transfer; only the final short packet ends the message.
  // Keep the RNDIS buffer owned until every chunk has really completed.
  bool submitted = _netd_itf.ecm_mode ? usbd_edpt_xfer(0, _netd_itf.ep_in, buf, len, false) : submit_rndis_chunk();
  if (!submitted) {
    in_renew_pending = false;
    in_xfer_bytes = 0; can_xmit = true; return false;
  }
  return true;
}

void netd_report(uint8_t *buf, uint16_t len) {
  const uint8_t rhport = 0;
  len = tu_min16(len, sizeof(ecm_notify_t));

  if (!usbd_edpt_claim(rhport, _netd_itf.ep_notif)) {
    TU_LOG1("ECM: Failed to claim notification endpoint\n");
    return;
  }

  memcpy(_netd_epbuf.notify, buf, len);
  usbd_edpt_xfer(rhport, _netd_itf.ep_notif, _netd_epbuf.notify, len, false);
}

//--------------------------------------------------------------------+
// USBD Driver API
//--------------------------------------------------------------------+
void netd_init(void) {
  tu_memclr(&_netd_itf, sizeof(_netd_itf));
  can_xmit = false;
  rx_renew_pending = false; in_xfer_bytes = 0; rx_renew_errors = 0;
  in_done_bytes = 0; in_chunk_bytes = 0; in_renew_pending = false;
  flea_rndis_reset_cb();
  tud_network_set_packet_filter_cb(0);
}

bool netd_deinit(void) {
  netd_init();
  return true;
}

void netd_reset(uint8_t rhport) {
  (void) rhport;
  netd_init();
}

uint16_t netd_open(uint8_t rhport, tusb_desc_interface_t const * itf_desc, uint16_t max_len) {
  bool const is_rndis = (TUD_RNDIS_ITF_CLASS    == itf_desc->bInterfaceClass    &&
                         TUD_RNDIS_ITF_SUBCLASS == itf_desc->bInterfaceSubClass &&
                         TUD_RNDIS_ITF_PROTOCOL == itf_desc->bInterfaceProtocol);

  bool const is_ecm = (TUSB_CLASS_CDC                           == itf_desc->bInterfaceClass &&
                       CDC_COMM_SUBCLASS_ETHERNET_CONTROL_MODEL == itf_desc->bInterfaceSubClass &&
                       0x00                                     == itf_desc->bInterfaceProtocol);

  TU_VERIFY(is_rndis || is_ecm, 0);

  // confirm interface hasn't already been allocated
  TU_ASSERT(0 == _netd_itf.ep_notif, 0);

  // sanity check the descriptor
  _netd_itf.ecm_mode = is_ecm;

  //------------- Management Interface -------------//
  _netd_itf.itf_num = itf_desc->bInterfaceNumber;

  uint16_t drv_len = sizeof(tusb_desc_interface_t);
  uint8_t const * p_desc = tu_desc_next( itf_desc );

  // Communication Functional Descriptors
  while (TUSB_DESC_CS_INTERFACE == tu_desc_type(p_desc) && drv_len <= max_len) {
    drv_len += tu_desc_len(p_desc);
    p_desc   = tu_desc_next(p_desc);
  }

  // notification endpoint (if any)
  if (TUSB_DESC_ENDPOINT == tu_desc_type(p_desc)) {
    TU_ASSERT(usbd_edpt_open(rhport, (tusb_desc_endpoint_t const *) p_desc), 0);

    _netd_itf.ep_notif = ((tusb_desc_endpoint_t const*)p_desc)->bEndpointAddress;

    drv_len += tu_desc_len(p_desc);
    p_desc = tu_desc_next(p_desc);
  }

  //------------- Data Interface -------------//
  // - RNDIS Data followed immediately by a pair of endpoints
  // - CDC-ECM data interface has 2 alternate settings
  //   - 0 : zero endpoints for inactive (default)
  //   - 1 : IN & OUT endpoints for active networking
  TU_ASSERT(TUSB_DESC_INTERFACE == tu_desc_type(p_desc), 0);

  do {
    tusb_desc_interface_t const * data_itf_desc = (tusb_desc_interface_t const *) p_desc;
    TU_ASSERT(TUSB_CLASS_CDC_DATA == data_itf_desc->bInterfaceClass, 0);

    drv_len += tu_desc_len(p_desc);
    p_desc   = tu_desc_next(p_desc);
  } while (_netd_itf.ecm_mode && (TUSB_DESC_INTERFACE == tu_desc_type(p_desc)) && (drv_len <= max_len));

  // Pair of endpoints
  TU_ASSERT(TUSB_DESC_ENDPOINT == tu_desc_type(p_desc), 0);

  // Save the actual bulk endpoint size (IN and OUT assumed equal)
  _netd_itf.ep_size = tu_edpt_packet_size((tusb_desc_endpoint_t const *) p_desc);

  if (_netd_itf.ecm_mode) {
    // ECM by default is in-active, save the endpoint attribute
    // to open later when received setInterface
    _netd_itf.ecm_desc_epdata = p_desc;
  } else {
    // Open endpoint pair for RNDIS
    TU_ASSERT(usbd_open_edpt_pair(rhport, p_desc, 2, TUSB_XFER_BULK, &_netd_itf.ep_out, &_netd_itf.ep_in), 0);

    // we are ready to transmit a packet
    can_xmit = true;

    // prepare for incoming packets
    tud_network_recv_renew();
  }

  drv_len += 2*sizeof(tusb_desc_endpoint_t);

  return drv_len;
}

static void ecm_report(bool nc) {
  ecm_notify_t ecm_notify_nc = {
    .header = {
      .bmRequestType = 0xA1,
      .bRequest = 0, /* NETWORK_CONNECTION aka NetworkConnection */
      .wValue = ecm_link_is_up ? 1 : 0,   /* Use current link state */
      .wLength = 0,
    },
  };

  const uint32_t link_bps = (tud_speed_get() == TUSB_SPEED_HIGH) ? 480000000U : 12000000U;
  const ecm_notify_t ecm_notify_csc = {
    .header = {
      .bmRequestType = 0xA1,
      .bRequest = 0x2A, /* CONNECTION_SPEED_CHANGE aka ConnectionSpeedChange */
      .wLength = 8,
    },
    .downlink = link_bps,
    .uplink = link_bps,
  };

  ecm_notify_t notify = (nc) ? ecm_notify_nc : ecm_notify_csc;
  notify.header.wIndex = _netd_itf.itf_num;
  netd_report((uint8_t *)&notify, (nc) ? sizeof(notify.header) : sizeof(notify));
}

// Invoked when a control transfer occurred on an interface of this class
// Driver response accordingly to the request and the transfer stage (setup/data/ack)
// return false to stall control endpoint (e.g unsupported request)
bool netd_control_xfer_cb (uint8_t rhport, uint8_t stage, tusb_control_request_t const * request) {
  if (stage == CONTROL_STAGE_SETUP) {
    HOTSPOT_VLOG("USB_CONTROL type=0x%02x request=0x%02x length=%u value=%u interface=%u", request->bmRequestType,
      request->bRequest, request->wLength, request->wValue, request->wIndex);
    switch (request->bmRequestType_bit.type) {
      case TUSB_REQ_TYPE_STANDARD:
        switch (request->bRequest) {
          case TUSB_REQ_GET_INTERFACE: {
            uint8_t const req_itfnum = (uint8_t)request->wIndex;
            TU_VERIFY(_netd_itf.itf_num+1 == req_itfnum);

            tud_control_xfer(rhport, request, &_netd_itf.itf_data_alt, 1);
          }
          break;

          case TUSB_REQ_SET_INTERFACE: {
            uint8_t const req_itfnum = (uint8_t)request->wIndex;
            uint8_t const req_alt = (uint8_t)request->wValue;

            // Only valid for Data Interface with Alternate is either 0 or 1
            TU_VERIFY(_netd_itf.itf_num+1 == req_itfnum && req_alt < 2);

            // ACM-ECM only: request to enable/disable network activities
            TU_VERIFY(_netd_itf.ecm_mode);

            _netd_itf.itf_data_alt = req_alt;

            if (_netd_itf.itf_data_alt) {
              // TODO since we don't actually close endpoint
              // hack here to not re-open it
              if (_netd_itf.ep_in == 0 && _netd_itf.ep_out == 0) {
                TU_ASSERT(_netd_itf.ecm_desc_epdata);
                TU_ASSERT(
                  usbd_open_edpt_pair(rhport, _netd_itf.ecm_desc_epdata, 2, TUSB_XFER_BULK, &_netd_itf.ep_out, &
                    _netd_itf.ep_in));

                // TODO should be merge with RNDIS's after endpoint opened
                // Also should have opposite callback for application to disable network !!
                can_xmit = true; // we are ready to transmit a packet
                tud_network_recv_renew(); // prepare for incoming packets
              }
            } else {
              // TODO close the endpoint pair
              // For now pretend that we did, this should have no harm since host won't try to
              // communicate with the endpoints again
              // _netd_itf.ep_in = _netd_itf.ep_out = 0
            }

            tud_control_status(rhport, request);
          }
          break;

          // unsupported request
          default: return false;
        }
        break;

      case TUSB_REQ_TYPE_CLASS:
        TU_VERIFY(_netd_itf.itf_num == request->wIndex);
        TU_VERIFY(flea_rndis_control_capacity(request->bmRequestType_bit.direction == TUSB_DIR_IN,
                                              request->wLength, NETD_CONTROL_SIZE));
        TU_VERIFY(request->bRequest == (request->bmRequestType_bit.direction == TUSB_DIR_IN ? 1 : 0));

        if (_netd_itf.ecm_mode) {
          /* the only required CDC-ECM Management Element Request is SetEthernetPacketFilter */
          if (0x43 /* SET_ETHERNET_PACKET_FILTER */ == request->bRequest) {
            tud_network_set_packet_filter_cb(request->wValue);
            tud_control_xfer(rhport, request, NULL, 0);
            // Only send connection notification if link is up
            if (ecm_link_is_up) {
              ecm_report(true);
            }
          }
        } else {
          if (request->bmRequestType_bit.direction == TUSB_DIR_IN) {
            rndis_generic_msg_t* rndis_msg = (rndis_generic_msg_t*)((void*)_netd_epbuf.ctrl);
            uint32_t msglen = tu_le32toh(rndis_msg->MessageLength);
            TU_ASSERT(msglen <= NETD_CONTROL_SIZE);
            tud_control_xfer(rhport, request, _netd_epbuf.ctrl, (uint16_t)msglen);
          } else {
            memset(_netd_epbuf.ctrl, 0, sizeof(_netd_epbuf.ctrl));
            tud_control_xfer(rhport, request, _netd_epbuf.ctrl, NETD_CONTROL_SIZE);
          }
        }
        break;

      // unsupported request
      default: return false;
    }
  } else if (stage == CONTROL_STAGE_DATA) {
    // Handle RNDIS class control OUT only
    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_CLASS &&
        request->bmRequestType_bit.direction == TUSB_DIR_OUT &&
        _netd_itf.itf_num == request->wIndex) {
      if (!_netd_itf.ecm_mode) {
        TU_VERIFY(flea_rndis_control(_netd_epbuf.ctrl, request->wLength, NETD_CONTROL_SIZE));
        rndis_class_set_handler(_netd_epbuf.ctrl, request->wLength);
      }
    }
  }

  return true;
}

static void handle_incoming_packet(uint32_t len) {
  uint8_t* pnt = _netd_epbuf.rx;
  uint32_t size = 0;

  if (_netd_itf.ecm_mode) {
    size = len;
  } else {
    uint32_t offset = 0;
    if (flea_rndis_packet(pnt, len, &offset, &size)) pnt += offset;
    else hotspotDiagnosticLog("RNDIS_RX_INVALID transfer_bytes=%lu", (unsigned long)len);

  }

  if (!tud_network_recv_cb(pnt, (uint16_t)size)) {
    /* if a buffer was never handled by user code, we must renew on the user's behalf */
    tud_network_recv_renew();
  }
}

bool netd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
  (void)rhport;
  if (result != XFER_RESULT_SUCCESS) {
    if (ep_addr == _netd_itf.ep_out) tud_network_recv_renew();
    if (ep_addr == _netd_itf.ep_in) {
      in_xfer_bytes = 0; in_done_bytes = 0; in_chunk_bytes = 0; in_renew_pending = false;
      can_xmit = true; flea_rndis_xmit_result_cb(false); flea_rndis_xmit_done_cb();
    }
    return true;
  }

  /* new packet received */
  if (ep_addr == _netd_itf.ep_out) {
    handle_incoming_packet(xferred_bytes);
  }

  /* data transmission finished */
  if (ep_addr == _netd_itf.ep_in) {
    if (!_netd_itf.ecm_mode) {
      if (!in_xfer_bytes || !in_chunk_bytes) return true;
      if (xferred_bytes != in_chunk_bytes) {
        hotspotDiagnosticLog("USB_IN_SHORT_COMPLETION expected=%u actual=%lu", in_chunk_bytes, (unsigned long)xferred_bytes);
        in_xfer_bytes = 0; in_done_bytes = 0; in_chunk_bytes = 0; in_renew_pending = false;
        can_xmit = true; flea_rndis_xmit_result_cb(false); flea_rndis_xmit_done_cb();
        return true;
      }
      in_done_bytes += in_chunk_bytes;
      in_chunk_bytes = 0;
      if (in_done_bytes < in_xfer_bytes) {
        if (!submit_rndis_chunk()) hotspotDiagnosticLog("USB_IN_CONTINUATION_REJECTED done=%u total=%u", in_done_bytes, in_xfer_bytes);
        return true;
      }
    }
    in_xfer_bytes = 0; in_done_bytes = 0;
    /* TinyUSB requires the class driver to implement ZLP (since ZLP usage is class-specific) */
    if (_netd_itf.ecm_mode && xferred_bytes > 0 && 0 == (xferred_bytes & (_netd_itf.ep_size-1))) {
      if (!do_in_xfer(NULL, 0)) {
        flea_rndis_xmit_result_cb(false); flea_rndis_xmit_done_cb();
      }
    } else {
      /* we're finally finished */
      can_xmit = true;
      flea_rndis_xmit_result_cb(true);
      flea_rndis_xmit_done_cb();
    }
  }

  if (_netd_itf.ecm_mode && (ep_addr == _netd_itf.ep_notif)) {
    // Notification transfer complete - endpoint is now free
    // Don't automatically send speed change notification after link state changes
  }

  return true;
}

void flea_rndis_service(void) {
  if (rx_renew_pending) tud_network_recv_renew();
  if (in_renew_pending && _netd_itf.ep_in && !usbd_edpt_busy(0, _netd_itf.ep_in)) submit_rndis_chunk();
  const bool in_busy = _netd_itf.ep_in && usbd_edpt_busy(0, _netd_itf.ep_in);
  const bool out_busy = _netd_itf.ep_out && usbd_edpt_busy(0, _netd_itf.ep_out);
  hotspotDiagnosticLog("USB_BULK ready=%d in_busy=%d out_busy=%d in_bytes=%u out_retry=%d renew_errors=%lu",
    can_xmit, in_busy, out_busy, in_xfer_bytes, rx_renew_pending, (unsigned long)rx_renew_errors);
  hotspotDiagnosticLog("USB_IN_PROGRESS total=%u done=%u chunk=%u retry=%d", in_xfer_bytes, in_done_bytes, in_chunk_bytes, in_renew_pending);
#if CONFIG_IDF_TARGET_ESP32S3
  if (_netd_itf.ep_in && _netd_itf.ep_out) {
    // Read-only register snapshot: never clear interrupts, abort transfers or
    // touch FIFO data while TinyUSB owns the controller.
    const unsigned in = tu_edpt_number(_netd_itf.ep_in) - 1;
    const unsigned out = tu_edpt_number(_netd_itf.ep_out) - 1;
    HOTSPOT_VLOG("USB_HW in_ctl=%08lx in_size=%08lx in_irq=%08lx fifo=%08lx out_ctl=%08lx out_size=%08lx out_irq=%08lx",
      (unsigned long)USB_DWC.in_eps[in].diepctl_reg.val,
      (unsigned long)USB_DWC.in_eps[in].dieptsiz_reg.val,
      (unsigned long)USB_DWC.in_eps[in].diepint_reg.val,
      (unsigned long)USB_DWC.in_eps[in].dtxfsts_reg.val,
      (unsigned long)USB_DWC.out_eps[out].doepctl_reg.val,
      (unsigned long)USB_DWC.out_eps[out].doeptsiz_reg.val,
      (unsigned long)USB_DWC.out_eps[out].doepint_reg.val);
  }
#endif
}

bool tud_network_can_xmit(uint16_t size) {
  return can_xmit && _netd_itf.ep_in && size <= CFG_TUD_NET_MTU;
}

void tud_network_xmit(void *ref, uint16_t arg) {
  (void)flea_rndis_try_xmit(ref, arg);
}

bool flea_rndis_try_xmit(void *ref, uint16_t arg) {
  if (!tud_network_can_xmit(arg)) return false;

  uint16_t len = (_netd_itf.ecm_mode) ? 0 : CFG_TUD_NET_PACKET_PREFIX_LEN;
  uint8_t* data = _netd_epbuf.tx + len;

  len += tud_network_xmit_cb(data, ref, arg);

  if (!_netd_itf.ecm_mode) {
    rndis_data_packet_t *hdr = (rndis_data_packet_t *) ((void*) _netd_epbuf.tx);
    memset(hdr, 0, sizeof(rndis_data_packet_t));
    hdr->MessageType = REMOTE_NDIS_PACKET_MSG;
    hdr->MessageLength = len;
    hdr->DataOffset = sizeof(rndis_data_packet_t) - offsetof(rndis_data_packet_t, DataOffset);
    hdr->DataLength = len - sizeof(rndis_data_packet_t);
  }

  // Windows RNDIS permits a one-byte zero terminator when the last packet
  // fills wMaxPacketSize. Keep MessageLength unchanged and append the byte to
  // this message, so the final chunk is short without a separate ZLP stage.
  if (!_netd_itf.ecm_mode && len % _netd_itf.ep_size == 0) _netd_epbuf.tx[len++] = 0;
  return do_in_xfer(_netd_epbuf.tx, len);
}

// Set the network link state (up/down) and notify the host
void tud_network_link_state(uint8_t rhport, bool is_up) {
  (void)rhport;

  if (_netd_itf.ecm_mode) {
    ecm_link_is_up = is_up;

    // For ECM mode, send network connection notification only
    // Don't trigger speed change notification for link state changes
    ecm_notify_t notify = {
      .header = {
        .bmRequestType = 0xA1,
        .bRequest = 0,        /* NETWORK_CONNECTION */
        .wValue = is_up ? 1 : 0,  /* 0 = disconnected, 1 = connected */
        .wLength = 0,
      },
    };
    notify.header.wIndex = _netd_itf.itf_num;
    netd_report((uint8_t *)&notify, sizeof(notify.header));
  } else {
    // For RNDIS mode, we would need to implement RNDIS status indication
    // This is more complex and requires RNDIS_INDICATE_STATUS_MSG
    // For now, RNDIS doesn't support dynamic link state changes
    (void)is_up;
  }
}

#endif
