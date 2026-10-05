#include "rndis_config.h"
#include "device/usbd_pvt.h"

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *count) {
  static const usbd_class_driver_t driver = {
    .name = "FleaByte RNDIS", .init = netd_init, .deinit = netd_deinit,
    .reset = netd_reset, .open = netd_open,
    .control_xfer_cb = netd_control_xfer_cb, .xfer_cb = netd_xfer_cb,
    .xfer_isr = NULL, .sof = NULL
  };
  *count = 1;
  return &driver;
}
