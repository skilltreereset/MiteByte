#pragma once
constexpr int INITR_MINI160x80 = 0;
class Adafruit_ST7735 {
public:
  template<class... T> explicit Adafruit_ST7735(T...) {}
  template<class... T> void drawFastHLine(T...) {}
  template<class... T> void drawFastVLine(T...) {}
  template<class... T> void fillRect(T...) {}
  template<class... T> void fillScreen(T...) {}
  template<class... T> void initR(T...) {}
  template<class... T> void invertDisplay(T...) {}
  template<class... T> void print(T...) {}
  template<class... T> void setCursor(T...) {}
  template<class... T> void setRotation(T...) {}
  template<class... T> void setTextColor(T...) {}
  template<class... T> void setTextSize(T...) {}
};
