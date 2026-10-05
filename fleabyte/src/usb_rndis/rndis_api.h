#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "tusb.h"
#include "device/usbd_pvt.h"
#ifdef __cplusplus
extern "C" {
#endif
extern uint8_t flea_rndis_mac_address[6];
bool flea_rndis_can_xmit(uint16_t size);
void flea_rndis_xmit(void *ref, uint16_t size);
bool flea_rndis_try_xmit(void *ref, uint16_t size);
void flea_rndis_recv_renew(void);
void flea_rndis_filter_cb(uint16_t filter);
bool flea_rndis_recv_cb(const uint8_t *data, uint16_t size);
uint16_t flea_rndis_xmit_cb(uint8_t *data, void *ref, uint16_t size);
void flea_rndis_xmit_done_cb(void);
void flea_rndis_xmit_result_cb(bool success);
void flea_rndis_reset_cb(void);
// Call only in the USB task. Has side effects: retries rejected OUT renewals
// and resubmits a stalled IN continuation, then logs controller state.
void flea_rndis_service(void);
#ifdef __cplusplus
}
#endif
