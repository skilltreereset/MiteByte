#pragma once
#include <stdint.h>
static inline bool hotspotSubnetsOverlap(uint32_t a, uint32_t maskA,
                                        uint32_t b, uint32_t maskB) {
  return !maskA || !maskB || (a & maskA) == (b & maskA) || (a & maskB) == (b & maskB);
}
