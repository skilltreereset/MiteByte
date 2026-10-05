#pragma once
#include <cstddef>
#include <cstdint>
#define FILE_APPEND "a"
#define FILE_WRITE "w"
class File {
public:
  const char *path = "";
  explicit operator bool() const { return true; }
  void close() {}
  bool isDirectory() const { return false; }
  const char *name() const { return ""; }
  size_t size() const { return 0; }
  File openNextFile() { return {}; }
  size_t println(const char *) { return 0; }
  size_t write(const uint8_t *, size_t);
  void flush() {}
};
