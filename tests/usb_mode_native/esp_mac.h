#pragma once
#include <cstdint>
inline void esp_efuse_mac_get_default(uint8_t *mac) {
  for (int i = 0; i < 6; ++i) mac[i] = i + 1;
}
