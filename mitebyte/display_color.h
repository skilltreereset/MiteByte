#pragma once
#include <stdint.h>
#include "config.h"

// 8-bit RGB to the panel's RGB565, honouring how this board wires red and blue.
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
#if TFT_SWAP_RED_BLUE
  return ((uint16_t)(b & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (r >> 3);
#else
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
#endif
}

// The one blue of the interface: the dashboard's frame and headings, and the
// menu's frame corners.
constexpr uint16_t C_EDGE = rgb(0x7A, 0xD4, 0xFB);
