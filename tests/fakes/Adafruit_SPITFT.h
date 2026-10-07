#pragma once
#include "Adafruit_GFX.h"
#include <cassert>
#include <vector>

// A panel that keeps what was pushed to it, so a test can look at the screen
// and at how much traffic it took to get there. 160x160 covers both
// orientations of the real panel.
class Adafruit_SPITFT : public Adafruit_GFX {
public:
  static constexpr int WIDTH_PX = 160, HEIGHT_PX = 160;

  Adafruit_SPITFT() : Adafruit_GFX(WIDTH_PX, HEIGHT_PX), shadow(WIDTH_PX * HEIGHT_PX, 0) {}

  // The dashboard draws straight to the panel; those calls are not tracked.
  void drawPixel(int16_t, int16_t, uint16_t) override {}
  void drawFastHLine(int16_t, int16_t, int16_t, uint16_t) override {}
  void drawFastVLine(int16_t, int16_t, int16_t, uint16_t) override {}
  void fillRect(int16_t, int16_t, int16_t, int16_t, uint16_t) override {}
  void fillScreen(uint16_t) override {}
  void startWrite() override { assert(!open); open = true; }
  void endWrite() override { assert(open); open = false; }

  virtual void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    assert(open);
    winX = x; winY = y; winW = w; winH = h;
  }
  void writePixels(uint16_t *colors, uint32_t len, bool = true, bool = false) {
    assert(open && len == (uint32_t)winW * winH);
    for (uint32_t i = 0; i < len; i++) {
      shadow[(winY + i / winW) * WIDTH_PX + winX + i % winW] = colors[i];
    }
    ++pushes;
    pixelsPushed += len;
  }

  uint16_t pixel(int x, int y) const { return shadow[y * WIDTH_PX + x]; }
  void resetCounters() { pushes = 0; pixelsPushed = 0; }

  std::vector<uint16_t> shadow;
  int pushes = 0;
  uint32_t pixelsPushed = 0;

private:
  bool open = false;
  int winX = 0, winY = 0, winW = 0, winH = 0;
};
