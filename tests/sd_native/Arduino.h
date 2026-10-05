#pragma once
#include "../fakes/Arduino.h"
#define DMA_ATTR
struct FakeSerial {
  template<class... Args> void printf(const char *, Args...) {}
  void println(const char *) {}
};
inline FakeSerial Serial;
