#pragma once
#include <cstdint>

struct GFXfont {};

// Just enough of the GFX base class for a view to subclass it. Primitives fall
// back on drawPixel as the real ones do; a glyph is a solid box, which is
// enough to tell where text lands and in what colour.
class Adafruit_GFX {
public:
  Adafruit_GFX(int16_t w, int16_t h) : WIDTH(w), HEIGHT(h), _width(w), _height(h) {}
  virtual ~Adafruit_GFX() = default;

  virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;
  virtual void startWrite() {}
  virtual void writePixel(int16_t x, int16_t y, uint16_t color) { drawPixel(x, y, color); }
  virtual void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    fillRect(x, y, w, h, color);
  }
  virtual void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    drawFastVLine(x, y, h, color);
  }
  virtual void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    drawFastHLine(x, y, w, color);
  }
  virtual void endWrite() {}
  virtual void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    for (int16_t i = 0; i < h; i++) drawPixel(x, y + i, color);
  }
  virtual void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    for (int16_t i = 0; i < w; i++) drawPixel(x + i, y, color);
  }
  virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    for (int16_t i = 0; i < w; i++) writeFastVLine(x + i, y, h, color);
  }
  virtual void fillScreen(uint16_t color) { fillRect(0, 0, _width, _height, color); }

  void setFont(const GFXfont * = nullptr) {}
  // Like the real size-1 glyphs, one writePixel at a time.
  void drawChar(int16_t x, int16_t y, unsigned char, uint16_t color, uint16_t, uint8_t) {
    for (int16_t row = 0; row < 11; row++)
      for (int16_t col = 0; col < 9; col++) writePixel(x + col, y - 10 + row, color);
  }
  int16_t width() const { return _width; }
  int16_t height() const { return _height; }

protected:
  int16_t WIDTH, HEIGHT, _width, _height;
};
