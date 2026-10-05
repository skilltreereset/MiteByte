#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

class String : public std::string {
public:
  using std::string::string;
  String() = default;
  String(const std::string &s) : std::string(s) {}
  explicit String(int n) : std::string(std::to_string(n)) {}
  bool isEmpty() const { return empty(); }
  String substring(size_t start, size_t end = npos) const {
    return substr(start, end == npos ? npos : end - start);
  }
  int indexOf(const String &s) const {
    auto pos = find(s);
    return pos == npos ? -1 : static_cast<int>(pos);
  }
  int lastIndexOf(char c) const {
    auto pos = rfind(c);
    return pos == npos ? -1 : static_cast<int>(pos);
  }
};

constexpr int HIGH = 1, LOW = 0, OUTPUT = 1;
uint32_t millis();
void delay(uint32_t ms);
int digitalRead(int pin);
void digitalWrite(int pin, int value);
void pinMode(int pin, int mode);
bool ledcAttach(int pin, int frequency, int bits);
void ledcWrite(int pin, int duty);
inline long random(long max) { return max > 0 ? max / 2 : 0; }
inline long random(long min, long max) { return min + random(max - min); }
struct TestESP { void restart(); };
extern TestESP ESP;
