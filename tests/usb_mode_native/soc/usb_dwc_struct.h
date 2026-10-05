#pragma once
#include <cstdint>
struct usb_dwc_dev_t { struct { volatile uint32_t val = 0; } pcgcctl_reg; };
extern usb_dwc_dev_t USB_DWC;
