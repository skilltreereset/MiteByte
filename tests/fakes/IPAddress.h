#pragma once
#include "Arduino.h"
class IPAddress {
  uint32_t value;
public:
  IPAddress(uint32_t ip = 0) : value(ip) {}
  operator uint32_t() const { return value; }
};
