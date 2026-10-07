#pragma once
#include "Adafruit_SPITFT.h"
constexpr int INITR_MINI160x80 = 0;
class Adafruit_ST7735 : public Adafruit_SPITFT {
public:
  template<class... T> explicit Adafruit_ST7735(T...) {}
  template<class... T> void drawRGBBitmap(T...) {}
  template<class... T> void initR(T...) {}
  template<class... T> void invertDisplay(T...) {}
  template<class... T> void print(T...) {}
  template<class... T> void setCursor(T...) {}
  template<class... T> void setRotation(T...) {}
  template<class... T> void setTextColor(T...) {}
  template<class... T> void setTextSize(T...) {}
protected:
  template<class... T> void setColRowStart(T...) {}
};
