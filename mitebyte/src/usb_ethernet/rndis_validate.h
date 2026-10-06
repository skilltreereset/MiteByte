#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// An IN request describes the host's receive capacity, not bytes written into
// our buffer. Windows posts at least 1024 bytes for encapsulated responses.
// TinyUSB sends only the prepared response; only OUT needs this size limit.
static inline bool flea_rndis_control_capacity(bool deviceToHost,
                                               uint16_t requested, uint16_t capacity) {
  return deviceToHost || requested <= capacity;
}

static inline uint32_t flea_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Host-controlled offsets are relative to byte 8. Check subtraction rather
// than adding untrusted offsets, including the declared message boundary.
static inline bool flea_rndis_window(uint32_t length, uint32_t offset,
                                     uint32_t size, uint32_t minimum) {
  return length >= minimum && offset >= minimum - 8 && offset <= length - 8 &&
         size <= length - 8 - offset;
}

static inline bool flea_rndis_packet(const uint8_t *p, uint32_t received,
                                     uint32_t *offset, uint32_t *length) {
  if (received < 44 || flea_le32(p) != 1) return false;
  uint32_t message = flea_le32(p + 4), dataOffset = flea_le32(p + 8);
  uint32_t dataLength = flea_le32(p + 12);
  if (message > received || dataLength < 14 || dataLength > 1514 ||
      !flea_rndis_window(message, dataOffset, dataLength, 44)) return false;
  *offset = 8 + dataOffset;
  *length = dataLength;
  return true;
}

static inline bool flea_rndis_control(const uint8_t *p, uint32_t received,
                                      uint32_t capacity) {
  if (received < 8 || received > capacity) return false;
  uint32_t length = flea_le32(p + 4), type = flea_le32(p);
  if (length > received || length < 8) return false;
  switch (type) {
    case 2: return length >= 24; // initialize
    case 3: return length >= 12; // halt
    case 4: return length >= 28; // query (input buffer is unused)
    case 5: { // set: OID payload must be within the received message
      if (length < 28) return false;
      uint32_t size = flea_le32(p + 16), offset = flea_le32(p + 20);
      if (flea_le32(p + 12) == 0x0001010e && size < 4) return false; // packet filter
      return (!size && !offset) || flea_rndis_window(length, offset, size, 28);
    }
    case 6: return length >= 8;  // reset
    case 8: return length >= 12; // keepalive
    default: return false;
  }
}
