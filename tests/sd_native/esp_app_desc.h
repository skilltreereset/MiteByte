#pragma once
#include <cstddef>
#include <cstring>
#include <cstdint>
struct esp_app_desc_t { uint8_t app_elf_sha256[32]; };
inline const esp_app_desc_t *esp_app_get_description() {
  static esp_app_desc_t description = {};
  for (size_t i = 0; i < 32; ++i) description.app_elf_sha256[i] = uint8_t(i);
  return &description;
}
inline int esp_app_get_elf_sha256(char *buffer, size_t size) {
  const char hash[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  if (size < sizeof(hash)) return 0;
  std::memcpy(buffer, hash, sizeof(hash)); return sizeof(hash);
}
