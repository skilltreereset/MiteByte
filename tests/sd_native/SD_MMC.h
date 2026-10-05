#pragma once
#include <cstdint>
#include "FS.h"
#define CARD_NONE 0
#define BOARD_MAX_SDMMC_FREQ 40000
class FakeSd {
public:
  template<class... Args> bool setPins(Args...) { return true; }
  bool begin(const char *, bool, bool, int, int);
  void end();
  int cardType();
  uint64_t cardSize() { return 1024ull * 1024 * 1024; }
  uint32_t numSectors() { return 1024; }
  uint32_t sectorSize();
  bool exists(const char *) { return true; }
  bool mkdir(const char *) { return false; }
  bool remove(const char *) { return true; }
  bool rename(const char *, const char *);
  File open(const char *path, const char * = "r") { File file; file.path = path; return file; }
  File open(const String &, const char * = "r") { return {}; }
  bool remove(const String &) { return false; }
  bool rmdir(const String &) { return false; }
  bool rmdir(const char *) { return false; }
};
inline FakeSd SD_MMC;
