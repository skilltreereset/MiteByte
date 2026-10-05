#pragma once
constexpr int FSPI = 0;
class SPIClass {
public:
  explicit SPIClass(int) {}
  template<class... T> void begin(T...) {}
};
