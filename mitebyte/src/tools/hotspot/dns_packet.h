#pragma once
#include <stdint.h>
#include <stddef.h>

static inline uint16_t dnsWord(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
static inline bool dnsSkipName(const uint8_t *p, size_t size, size_t *offset) {
  size_t labels = 0;
  while (*offset < size) {
    uint8_t length = p[(*offset)++];
    if (!length) return true;
    if ((length & 0xc0) == 0xc0) {
      if (*offset >= size) return false;
      size_t target = ((size_t)(length & 0x3f) << 8) | p[(*offset)++];
      return target >= 12 && target < size;
    }
    if ((length & 0xc0) || length > size - *offset || ++labels > 127) return false;
    *offset += length;
  }
  return false;
}
// Advertise a UDP size that fits one Ethernet frame. Upstream resolvers can
// return TC for larger answers; clients then use our streaming TCP proxy.
static inline bool hotspotDnsPrepare(uint8_t *p, size_t size) {
  if (size < 12 || (p[2] & 0xf8) || dnsWord(p + 4) != 1) return false;
  size_t offset = 12;
  if (!dnsSkipName(p, size, &offset) || size - offset < 4) return false;
  offset += 4;
  unsigned records = dnsWord(p + 6) + dnsWord(p + 8) + dnsWord(p + 10);
  if (records > 128) return false;
  for (unsigned i = 0; i < records; ++i) {
    if (!dnsSkipName(p, size, &offset) || size - offset < 10) return false;
    if (dnsWord(p + offset) == 41 && dnsWord(p + offset + 2) > 1232) {
      p[offset + 2] = 1232 >> 8; p[offset + 3] = 1232 & 0xff;
    }
    size_t length = dnsWord(p + offset + 8);
    offset += 10;
    if (length > size - offset) return false;
    offset += length;
  }
  return offset == size;
}
