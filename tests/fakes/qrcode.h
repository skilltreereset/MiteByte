#pragma once
using esp_qrcode_handle_t = void *;
struct esp_qrcode_config_t {
  void (*display_func)(esp_qrcode_handle_t);
  int max_qrcode_version;
  int qrcode_ecc_level;
};
constexpr int ESP_QRCODE_ECC_LOW = 0, ESP_OK = 0;
inline int esp_qrcode_get_size(esp_qrcode_handle_t) { return 21; }
inline bool esp_qrcode_get_module(esp_qrcode_handle_t, int, int) { return false; }
inline int esp_qrcode_generate(esp_qrcode_config_t *cfg, const char *) {
  cfg->display_func(nullptr);
  return ESP_OK;
}
