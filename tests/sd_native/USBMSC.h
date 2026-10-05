#pragma once
#include <cstdint>
class USBMSC {
public:
  void vendorID(const char *) {}
  void productID(const char *) {}
  void productRevision(const char *) {}
  void onRead(int32_t (*)(uint32_t,uint32_t,void *,uint32_t)) {}
  void onWrite(int32_t (*)(uint32_t,uint32_t,uint8_t *,uint32_t)) {}
  void onStartStop(bool (*)(uint8_t,bool,bool)) {}
  bool begin(uint32_t, uint32_t) { return true; }
  void mediaPresent(bool present);
};
