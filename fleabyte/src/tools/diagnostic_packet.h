#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
// Protocol headers only: no DNS names, payload bytes, keys or passwords.
static inline unsigned diagnosticWord(const uint8_t *p) { return unsigned(p[0]) * 256 + p[1]; }
static inline bool hotspotDiagnosticPacketSummary(const uint8_t *p, size_t size, char *out, size_t capacity) {
  if (size < 14) return false;
  unsigned type = diagnosticWord(p + 12);
  if (type == 0x806) {
    if (size < 42 || diagnosticWord(p + 14) != 1 || diagnosticWord(p + 16) != 0x800 || p[18] != 6 || p[19] != 4) return false;
    snprintf(out, capacity, "ARP op=%u sender=%u.%u.%u.%u target=%u.%u.%u.%u", diagnosticWord(p + 20),
       p[28],p[29],p[30],p[31],p[38],p[39],p[40],p[41]); return true;
  }
  if (type != 0x800 || size < 34 || (p[14] >> 4) != 4) return false;
  size_t header = (p[14] & 15) * 4;
  unsigned length = diagnosticWord(p + 16);
  if (header < 20 || length < header || length > size - 14 || (diagnosticWord(p + 20) & 0x1fff)) return false;
  if (p[23] != 17 || length - header < 8) return false;
  size_t udp = 14 + header;
  unsigned src = diagnosticWord(p + udp), dst = diagnosticWord(p + udp + 2), bytes = diagnosticWord(p + udp + 4);
  if (bytes < 8 || bytes > length - header) return false;
  bool dns = src == 53 || dst == 53, dhcp = src == 67 || src == 68 || dst == 67 || dst == 68;
  if (!dns && !dhcp) return false;
  unsigned id = 0, kind = 0;
  if (dns && bytes >= 20) { id = diagnosticWord(p + udp + 8); kind = p[udp + 11] & 15; }
  if (dhcp && bytes >= 16) {
    id = (unsigned(p[udp+12]) << 24) | (unsigned(p[udp+13]) << 16) | diagnosticWord(p + udp + 14);
    if (bytes >= 248 && p[udp+244]==99 && p[udp+245]==130 && p[udp+246]==83 && p[udp+247]==99) {
      size_t option=udp+248, end=udp+bytes;
      while(option<end) {
        unsigned tag=p[option++]; if(tag==255) break; if(!tag) continue;
        if(option>=end) break;
        unsigned count=p[option++]; if(count>end-option) break;
        if(tag==53 && count==1) kind=p[option];
        option+=count;
      }
    }
  }
  snprintf(out, capacity, "%s %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u bytes=%u id=%u code=%u",
    dns ? "DNS" : "DHCP", p[26],p[27],p[28],p[29],src,p[30],p[31],p[32],p[33],dst,bytes,id,kind);
  return true;
}
