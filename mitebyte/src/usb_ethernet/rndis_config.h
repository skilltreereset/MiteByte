#pragma once
// Compile only this application driver as RNDIS. Arduino's built-in NCM
// driver retains its own configuration and symbols.
#include "tusb_option.h"
#undef CFG_TUD_NCM
#define CFG_TUD_NCM 0
#undef CFG_TUD_ECM_RNDIS
#define CFG_TUD_ECM_RNDIS 1
#define tud_network_recv_renew flea_rndis_recv_renew
#define tud_network_can_xmit flea_rndis_can_xmit
#define tud_network_xmit flea_rndis_xmit
#define tud_network_recv_cb flea_rndis_recv_cb
#define tud_network_xmit_cb flea_rndis_xmit_cb
#define tud_network_init_cb flea_rndis_init_cb
#define tud_network_mac_address flea_rndis_mac_address
#define tud_network_set_packet_filter_cb flea_rndis_filter_cb
#define tud_network_link_state flea_rndis_link_state
#define netd_init flea_rndis_init
#define netd_deinit flea_rndis_deinit
#define netd_reset flea_rndis_reset
#define netd_open flea_rndis_open
#define netd_control_xfer_cb flea_rndis_control_xfer_cb
#define netd_xfer_cb flea_rndis_xfer_cb
#define netd_report flea_rndis_report
#define rndis_class_set_handler flea_rndis_class_set_handler
#include "tusb.h"
// Microsoft's Windows 10/11 RNDIS class match, including in a composite device.
#undef TUD_RNDIS_ITF_CLASS
#undef TUD_RNDIS_ITF_SUBCLASS
#undef TUD_RNDIS_ITF_PROTOCOL
#define TUD_RNDIS_ITF_CLASS 0xEF
#define TUD_RNDIS_ITF_SUBCLASS 0x04
#define TUD_RNDIS_ITF_PROTOCOL 0x01
