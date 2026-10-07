// The button menu view. Reading order: palette and layout, fonts, strip
// canvas, scene, painting, animation state, public API.
//
// A frame is described once as a Scene (plain numbers: card rectangles,
// colours, text positions, all the geometry and easing), then painted a strip
// at a time into a small static buffer that is pushed to the panel. Painting
// skips anything that does not touch the strip being drawn, so ten strips cost
// about what one full frame would.

#include "menu_view.h"
#include "config.h"
#include "display_color.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SPITFT.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {

// ---- Palette ---------------------------------------------------------------

constexpr uint16_t C_BG = rgb(0x00, 0x00, 0x00);
constexpr uint16_t C_CARD = rgb(0x06, 0x14, 0x30);
constexpr uint16_t C_BOX = rgb(0x05, 0x0D, 0x1F);
constexpr uint16_t C_PINK = rgb(0xFF, 0x3D, 0x9A);
// C_EDGE, the interface blue of the frame corners, comes from display_color.h.
constexpr uint16_t C_UNDER = rgb(0x19, 0xE6, 0xFF);
constexpr uint16_t C_SELECTED = rgb(0xFF, 0xFF, 0xFF);
constexpr uint16_t C_CARD_TEXT = rgb(0xD3, 0xE2, 0xFF);
constexpr uint16_t C_RULER = rgb(0x4A, 0x7B, 0xD0);
constexpr uint16_t C_PRESS_LINE = C_SELECTED;
constexpr uint16_t C_PRESS_SHADOW = C_UNDER;
// The menu's pink also marks everything that is happening: the fill while
// the button is held, a running tool, and the messages that replace a label.
constexpr uint16_t C_PRESS_FILL = C_PINK;
constexpr uint16_t C_RUNNING = C_PINK;
constexpr uint16_t C_ALERT = C_PINK;
// What the selection reads when it is the running tool: holding stops it.
constexpr char STOP_LABEL[] = "STOP";

// ---- Timing ----------------------------------------------------------------

constexpr uint32_t FRAME_MS = 33;           // at most 30 frames a second
constexpr float ROLL_RATE = 16.0f;          // exponential ease, per second
constexpr float ROLL_SNAP = 0.002f;         // close enough to have arrived
constexpr float MAX_DT = 0.05f;             // a stalled loop must not jump the roll
constexpr uint32_t MARQUEE_DELAY_MS = 600;  // after a new selection
constexpr float MARQUEE_RATE = 0.35f;       // sweeps per second, out and back
constexpr uint32_t FLASH_MS = 300;          // the fill lingers after the release
constexpr uint32_t FILL_DELAY_MS = 150;     // a tap is shorter; the fill starts after this

// ---- Layout ----------------------------------------------------------------
// Two layouts from one description: landscape 160x80 and portrait 80x160. In
// both the list runs down the screen with the ruler on the left.

constexpr int16_t SEL_GAP = 4;
// FreeMonoBold9pt: from the tallest ascender to the baseline row is 12 rows,
// and each character advances 11 px.
constexpr int16_t TEXT_ASCENT = 11;
constexpr int16_t TEXT_ADV = 11;
constexpr int16_t FRAME_H = 1 + SEL_GAP + (TEXT_ASCENT + 2) + SEL_GAP + 1;
constexpr int16_t FRAME_GROW = 6;  // extra height while the list is moving
constexpr int16_t CORNER_ARM = 5;
constexpr int16_t CORNER_THICK = 2;
constexpr int16_t RAIL_THICK = 2;  // the pink lines along the frame's top and bottom
constexpr int16_t CARD_EDGE = 2;   // and along the outer edge of each card

constexpr int16_t RULER_X = 1;
constexpr int16_t RULER_STEP = 10;  // px per entry
constexpr int16_t PTR_W = 7, PTR_H = 2;
constexpr int16_t FX = RULER_X + PTR_W + 2;  // left edge of the frame and cards

constexpr int MAX_CARDS = 13;  // 2 * (portrait cards + 2) + 1
constexpr int WRAP_MIN = 5;    // fewer entries would show the same one twice

struct Layout {
  int16_t w, h, stripH;
  int16_t fw, cx, maxW;           // frame width, its centre, widest label
  int16_t frameY;                 // top of the frame at rest
  float frameC;                   // its centre line
  int16_t textBase;               // baseline of the selected label
  uint8_t cardCount;              // cards each side of the frame
  int16_t cardH;
  float step1, step2;             // frame to first card, card to card
  int16_t rulerLimit, rulerFade;  // ticks stop and fade out by these distances
};

constexpr Layout makeLayout(bool portrait) {
  Layout l{};
  l.w = portrait ? 80 : 160;
  l.h = portrait ? 160 : 80;
  l.stripH = portrait ? 16 : 8;
  l.fw = l.w - 2 - FX;
  l.cx = FX + l.fw / 2;
  l.maxW = l.fw - 10;
  l.frameY = (l.h - FRAME_H + 1) / 2;
  l.frameC = l.frameY + FRAME_H / 2.0f;
  l.textBase = l.frameY + 1 + SEL_GAP + TEXT_ASCENT;
  l.cardCount = portrait ? 4 : 2;
  const int16_t above = l.frameY;
  const int16_t below = l.h - l.frameY - FRAME_H;
  l.cardH = (above < below ? above : below) / l.cardCount;
  l.step1 = FRAME_H / 2.0f + l.cardH / 2.0f;
  l.step2 = l.cardH;
  l.rulerLimit = portrait ? 50 : 34;
  l.rulerFade = portrait ? 60 : 40;
  return l;
}

constexpr Layout LANDSCAPE = makeLayout(false);
constexpr Layout PORTRAIT = makeLayout(true);

// ---- Fonts -----------------------------------------------------------------
// Card labels use a 5x7 pixel font, squeezed to six rows so a card has room
// around its text. One row of bits per column, least significant bit on top,
// ASCII 0x20 to 0x7E. The selected label uses FreeMonoBold9pt instead.

constexpr int16_t CARD_ADV = 6;
constexpr int16_t CARD_ROWS = 6;

constexpr uint8_t FONT_5X7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00},  // space
    {0x00,0x00,0x5F,0x00,0x00},  // !
    {0x00,0x07,0x00,0x07,0x00},  // "
    {0x14,0x7F,0x14,0x7F,0x14},  // #
    {0x24,0x2A,0x7F,0x2A,0x12},  // $
    {0x23,0x13,0x08,0x64,0x62},  // %
    {0x36,0x49,0x55,0x22,0x50},  // &
    {0x00,0x05,0x03,0x00,0x00},  // '
    {0x00,0x1C,0x22,0x41,0x00},  // (
    {0x00,0x41,0x22,0x1C,0x00},  // )
    {0x14,0x08,0x3E,0x08,0x14},  // *
    {0x08,0x08,0x3E,0x08,0x08},  // +
    {0x00,0x50,0x30,0x00,0x00},  // ,
    {0x08,0x08,0x08,0x08,0x08},  // -
    {0x00,0x60,0x60,0x00,0x00},  // .
    {0x20,0x10,0x08,0x04,0x02},  // /
    {0x3E,0x51,0x49,0x45,0x3E},  // 0
    {0x00,0x42,0x7F,0x40,0x00},  // 1
    {0x42,0x61,0x51,0x49,0x46},  // 2
    {0x21,0x41,0x45,0x4B,0x31},  // 3
    {0x18,0x14,0x12,0x7F,0x10},  // 4
    {0x27,0x45,0x45,0x45,0x39},  // 5
    {0x3C,0x4A,0x49,0x49,0x30},  // 6
    {0x01,0x71,0x09,0x05,0x03},  // 7
    {0x36,0x49,0x49,0x49,0x36},  // 8
    {0x06,0x49,0x49,0x29,0x1E},  // 9
    {0x00,0x36,0x36,0x00,0x00},  // :
    {0x00,0x56,0x36,0x00,0x00},  // ;
    {0x08,0x14,0x22,0x41,0x00},  // <
    {0x14,0x14,0x14,0x14,0x14},  // =
    {0x00,0x41,0x22,0x14,0x08},  // >
    {0x02,0x01,0x51,0x09,0x06},  // ?
    {0x32,0x49,0x79,0x41,0x3E},  // @
    {0x7E,0x11,0x11,0x11,0x7E},  // A
    {0x7F,0x49,0x49,0x49,0x36},  // B
    {0x3E,0x41,0x41,0x41,0x22},  // C
    {0x7F,0x41,0x41,0x22,0x1C},  // D
    {0x7F,0x49,0x49,0x49,0x41},  // E
    {0x7F,0x09,0x09,0x09,0x01},  // F
    {0x3E,0x41,0x49,0x49,0x7A},  // G
    {0x7F,0x08,0x08,0x08,0x7F},  // H
    {0x00,0x41,0x7F,0x41,0x00},  // I
    {0x20,0x40,0x41,0x3F,0x01},  // J
    {0x7F,0x08,0x14,0x22,0x41},  // K
    {0x7F,0x40,0x40,0x40,0x40},  // L
    {0x7F,0x02,0x0C,0x02,0x7F},  // M
    {0x7F,0x04,0x08,0x10,0x7F},  // N
    {0x3E,0x41,0x41,0x41,0x3E},  // O
    {0x7F,0x09,0x09,0x09,0x06},  // P
    {0x3E,0x41,0x51,0x21,0x5E},  // Q
    {0x7F,0x09,0x19,0x29,0x46},  // R
    {0x46,0x49,0x49,0x49,0x31},  // S
    {0x01,0x01,0x7F,0x01,0x01},  // T
    {0x3F,0x40,0x40,0x40,0x3F},  // U
    {0x1F,0x20,0x40,0x20,0x1F},  // V
    {0x3F,0x40,0x38,0x40,0x3F},  // W
    {0x63,0x14,0x08,0x14,0x63},  // X
    {0x07,0x08,0x70,0x08,0x07},  // Y
    {0x61,0x51,0x49,0x45,0x43},  // Z
    {0x00,0x7F,0x41,0x41,0x00},  // [
    {0x02,0x04,0x08,0x10,0x20},  // backslash
    {0x00,0x41,0x41,0x7F,0x00},  // ]
    {0x04,0x02,0x01,0x02,0x04},  // ^
    {0x40,0x40,0x40,0x40,0x40},  // _
    {0x00,0x01,0x02,0x04,0x00},  // backtick
    {0x20,0x54,0x54,0x54,0x78},  // a
    {0x7F,0x48,0x44,0x44,0x38},  // b
    {0x38,0x44,0x44,0x44,0x20},  // c
    {0x38,0x44,0x44,0x48,0x7F},  // d
    {0x38,0x54,0x54,0x54,0x18},  // e
    {0x08,0x7E,0x09,0x01,0x02},  // f
    {0x0C,0x52,0x52,0x52,0x3E},  // g
    {0x7F,0x08,0x04,0x04,0x78},  // h
    {0x00,0x44,0x7D,0x40,0x00},  // i
    {0x20,0x40,0x44,0x3D,0x00},  // j
    {0x7F,0x10,0x28,0x44,0x00},  // k
    {0x00,0x41,0x7F,0x40,0x00},  // l
    {0x7C,0x04,0x18,0x04,0x78},  // m
    {0x7C,0x08,0x04,0x04,0x78},  // n
    {0x38,0x44,0x44,0x44,0x38},  // o
    {0x7C,0x14,0x14,0x14,0x08},  // p
    {0x08,0x14,0x14,0x18,0x7C},  // q
    {0x7C,0x08,0x04,0x04,0x08},  // r
    {0x48,0x54,0x54,0x54,0x20},  // s
    {0x04,0x3F,0x44,0x40,0x20},  // t
    {0x3C,0x40,0x40,0x20,0x7C},  // u
    {0x1C,0x20,0x40,0x20,0x1C},  // v
    {0x3C,0x40,0x30,0x40,0x3C},  // w
    {0x44,0x28,0x10,0x28,0x44},  // x
    {0x0C,0x50,0x50,0x50,0x3C},  // y
    {0x44,0x64,0x54,0x4C,0x44},  // z
    {0x00,0x08,0x36,0x41,0x00},  // {
    {0x00,0x00,0x7F,0x00,0x00},  // |
    {0x00,0x41,0x36,0x08,0x00},  // }
    {0x10,0x08,0x08,0x10,0x08},  // ~
};

// ---- Strip canvas ----------------------------------------------------------
// An Adafruit_GFX surface over one horizontal slice of the screen. Drawing
// calls use screen coordinates and are clipped to the slice, so text and lines
// can be drawn with the ordinary GFX calls and simply land in the right place.
// Landscape slices are 160x8 and portrait ones 80x16: the same 1280 pixels.

constexpr int STRIP_PIXELS = 1280;
uint16_t s_stripBuf[STRIP_PIXELS];
static_assert(sizeof(s_stripBuf) == 2560, "strip buffer is 2.5 KiB");
static_assert(LANDSCAPE.w * LANDSCAPE.stripH == STRIP_PIXELS, "landscape strip");
static_assert(PORTRAIT.w * PORTRAIT.stripH == STRIP_PIXELS, "portrait strip");

class StripCanvas : public Adafruit_GFX {
 public:
  StripCanvas() : Adafruit_GFX(LANDSCAPE.w, LANDSCAPE.h) {}

  void configure(const Layout &l) {
    _width = WIDTH = l.w;
    _height = HEIGHT = l.h;
    stripH_ = l.stripH;
    clearClip();
  }
  void setStrip(int16_t top) { top_ = top; }
  int16_t top() const { return top_; }
  int16_t bottom() const { return top_ + stripH_; }

  // Horizontal window that drawing is cut to, [left, right).
  void setClip(int16_t left, int16_t right) { clipL_ = left; clipR_ = right; }
  void clearClip() { clipL_ = 0; clipR_ = _width; }

  // Restrict single pixels (so text) to the inside or the outside of a disc.
  // Centre and radius squared are doubled, which keeps half-pixel centres
  // exact. Rectangles are not affected.
  void setDisc(int32_t cx2, int32_t cy2, int32_t r2, bool inside) {
    disc_ = true;
    discCx2_ = cx2;
    discCy2_ = cy2;
    discR2_ = r2;
    discInside_ = inside;
  }
  void clearDisc() { disc_ = false; }
  int16_t clipLeft() const { return clipL_; }
  int16_t clipRight() const { return clipR_; }

  void clear(uint16_t color) {
    for (int i = 0; i < _width * stripH_; i++) s_stripBuf[i] = color;
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    const int16_t row = y - top_;
    if (x < clipL_ || x >= clipR_ || row < 0 || row >= stripH_) return;
    if (disc_) {
      const int32_t dx = 2 * x - discCx2_, dy = 2 * y - discCy2_;
      if ((dx * dx + dy * dy < discR2_) != discInside_) return;
    }
    s_stripBuf[row * _width + x] = color;
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    const int16_t x0 = x > clipL_ ? x : clipL_;
    const int16_t x1 = x + w < clipR_ ? x + w : clipR_;
    const int16_t y0 = y - top_ > 0 ? y - top_ : 0;
    const int16_t y1 = y - top_ + h < stripH_ ? y - top_ + h : stripH_;
    for (int16_t row = y0; row < y1; row++) {
      uint16_t *p = s_stripBuf + row * _width;
      for (int16_t col = x0; col < x1; col++) p[col] = color;
    }
  }
  // GFX routes its lines, rectangles and glyphs through these.
  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    fillRect(x, y, w, h, color);
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    fillRect(x, y, w, 1, color);
  }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    fillRect(x, y, 1, h, color);
  }
  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    fillRect(x, y, w, 1, color);
  }
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    fillRect(x, y, 1, h, color);
  }

 private:
  int16_t top_ = 0, stripH_ = LANDSCAPE.stripH;
  int16_t clipL_ = 0, clipR_ = 0;
  bool disc_ = false, discInside_ = true;
  int32_t discCx2_ = 0, discCy2_ = 0, discR2_ = 0;
};

StripCanvas s_canvas;

// ---- Small helpers ---------------------------------------------------------

inline int jsRound(float v) { return (int)floorf(v + 0.5f); }  // ties go up
inline int imax(int a, int b) { return a > b ? a : b; }
inline int imin(int a, int b) { return a < b ? a : b; }

// Floor of the square root, exactly.
int isqrt(int32_t n) {
  int s = (int)sqrtf((float)n);
  while (s * s > n) s--;
  while ((s + 1) * (s + 1) <= n) s++;
  return s;
}
inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// `fg` over `bg` at the given opacity, channel by channel in RGB565.
uint16_t mix(uint16_t fg, uint16_t bg, float opacity) {
  const uint32_t a = (uint32_t)(clamp01(opacity) * 256.0f + 0.5f);
  const uint32_t b = 256 - a;
  const uint32_t r = ((fg >> 11) * a + (bg >> 11) * b) >> 8;
  const uint32_t g = (((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * b) >> 8;
  const uint32_t bl = ((fg & 0x1F) * a + (bg & 0x1F) * b) >> 8;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// The entry shown at list position k, or false when k is off the end of a
// list too short to wrap.
bool itemIndex(int k, int n, bool wrap, int &idx) {
  if (n <= 0) return false;
  if (wrap) {
    idx = ((k % n) + n) % n;
    return true;
  }
  if (k < 0 || k >= n) return false;
  idx = k;
  return true;
}

// ---- Scene -----------------------------------------------------------------
// Everything a frame needs, already worked out. Zeroed before it is filled so
// two equal scenes are byte-for-byte equal, which is how a frame that would
// look the same as the last one is skipped.

struct CardView {
  int16_t x, y, w, h;
  uint16_t fill, outer, inner, text;
  bool above;           // the pink outer edge is the top row, else the bottom
  int16_t textX, textY;  // first column and top row of the label
  int16_t clipW;         // label cut to this width, 0 when it fits
  const char *label;
};

struct TextLayer {
  int16_t dx, dy;
  uint16_t color;
};

struct Scene {
  uint8_t nCards;
  CardView cards[MAX_CARDS];  // far to near
  int16_t rulerOff;           // row of a major tick near the centre line
  int16_t frameX, frameY, frameW, frameH;
  int32_t fillR2;     // the hold's fill: a disc from the frame's middle, doubled radius squared
  const char *label;  // null when nothing is selected
  int16_t labelX, clipL, clipR;
  uint8_t nLayers;
  TextLayer layers[3];  // painted in order, the last one on top
};

// The label where the fill has reached: white with a cyan shadow two rows deep.
constexpr TextLayer FILLED_LAYERS[3] = {
    {0, 1, C_PRESS_SHADOW}, {0, 2, C_PRESS_SHADOW}, {0, 0, C_SELECTED}};

// ---- Animation state -------------------------------------------------------

const std::vector<MenuItem> *s_items = nullptr;
bool s_active = false;
bool s_landscape = true;
size_t s_selected = 0;
size_t s_count = 0;
int32_t s_target = 0;  // list position being rolled to
float s_pos = 0;       // list position on screen, trailing the target
const char *s_alert = nullptr;
uint32_t s_selectedAt = 0;  // for the marquee
uint32_t s_animAt = 0;
uint32_t s_nextFrame = 0;
uint32_t s_heldMs = 0;  // the button, as last reported
bool s_filled = false;  // held long enough to select
uint32_t s_flashUntil = 0;
bool s_haveDrawn = false;
uint32_t s_drawnHash = 0;
float s_drawnPos = 0;

const Layout &layoutFor(bool landscape) { return landscape ? LANDSCAPE : PORTRAIT; }

// Where the selected label has slid to: it waits at the first letter, slides
// to the last, waits, and slides back. 0 is the start and 1 the far end.
float marqueeProgress(uint32_t now) {
  const int32_t since = (int32_t)(now - s_selectedAt) - (int32_t)MARQUEE_DELAY_MS;
  if (since <= 0) return 0;
  const float u = fmodf(since / 1000.0f * MARQUEE_RATE, 2.0f);
  const float tri = u > 1.0f ? 2.0f - u : u;
  return clamp01((tri - 0.15f) / 0.7f);
}

// How much of the frame the hold has filled, 0 to 1: nothing for a tap, then a
// sweep that completes just as the hold becomes long enough to select, and
// full again for the flash after the release.
float fillProgress(uint32_t now) {
  if ((int32_t)(s_flashUntil - now) > 0) return 1.0f;
  if (s_heldMs < FILL_DELAY_MS || s_heldMs >= SCREEN_LOCK_ENTER_HOLD_MS) return 0.0f;
  return clamp01((float)(s_heldMs - FILL_DELAY_MS) / (float)(MENU_HOLD_MS - FILL_DELAY_MS));
}

void addCard(Scene &sc, const Layout &l, const MenuItem &item, float rel) {
  const float a = fabsf(rel);
  const float beyond = a > 1.0f ? a - 1.0f : 0.0f;
  const bool above = rel < 0;
  const float fade = clamp01(1.0f - a * 0.2f);
  const float yc = l.frameC + (above ? -1.0f : 1.0f) * ((a < 1.0f ? a : 1.0f) * l.step1 + beyond * l.step2);
  if (yc + l.cardH / 2.0f < -8 || yc - l.cardH / 2.0f > l.h + 8) return;
  if (sc.nCards >= MAX_CARDS) return;

  CardView &c = sc.cards[sc.nCards++];
  c.w = imax(24, jsRound(l.fw - 8 - beyond * 10));
  c.x = jsRound(l.cx - c.w / 2.0f);
  c.y = jsRound(yc - l.cardH / 2.0f);
  c.h = l.cardH;
  c.above = above;

  c.fill = mix(C_CARD, C_BG, 0.92f * fade + 0.05f);
  c.outer = mix(C_PINK, c.fill, 0.55f * fade);
  c.inner = mix(C_SELECTED, c.fill, 0.08f * fade);
  c.text = item.on ? C_RUNNING : mix(C_CARD_TEXT, c.fill, fade > 0.85f ? fade : 0.85f);

  c.label = item.label.c_str();
  const int textW = (int)item.label.length() * CARD_ADV - 1;
  const int room = c.w - 6;
  const bool over = textW > room;
  c.textX = jsRound(c.x + c.w / 2.0f - (over ? room : textW) / 2.0f);
  c.textY = jsRound(yc - 3.0f);
  c.clipW = over ? room : 0;
}

void buildScene(Scene &sc, const Layout &l, uint32_t now) {
  memset(&sc, 0, sizeof sc);
  const std::vector<MenuItem> &items = *s_items;
  const int n = (int)items.size();
  const bool wrap = n >= WRAP_MIN;
  const float pos = s_pos;
  const int b = jsRound(pos);

  // Cards, farthest first. The entry nearest the middle is the selection and
  // belongs to the frame.
  const int reach = l.cardCount + 2;
  for (int dist = reach; dist >= 0; dist--) {
    for (int side = -1; side <= 1; side += 2) {
      if (dist == 0 && side > 0) break;
      const int k = b + side * dist;
      const float rel = k - pos;
      int idx;
      if (fabsf(rel) < 0.5f || !itemIndex(k, n, wrap, idx)) continue;
      addCard(sc, l, items[idx], rel);
    }
  }

  sc.rulerOff = (int16_t)jsRound(l.h / 2 - pos * RULER_STEP);

  // The frame grows while the list is still moving.
  const float moving = fabsf(s_target - pos);
  const int grow = jsRound((moving < 1.0f ? moving : 1.0f) * FRAME_GROW);
  sc.frameH = FRAME_H + grow;
  sc.frameW = l.fw;
  sc.frameX = jsRound(l.cx - l.fw / 2.0f);
  sc.frameY = jsRound(l.frameC - sc.frameH / 2.0f);
  // The fill is a disc grown from the middle of the frame until it clears the
  // corners (a pixel beyond, so the last frame is solid).
  const float cover = hypotf(sc.frameW / 2.0f, sc.frameH / 2.0f) + 1.0f;
  const float radius2 = 2.0f * fillProgress(now) * cover;
  sc.fillR2 = (int32_t)(radius2 * radius2 + 0.5f);

  int sel;
  const float off = fabsf(b - pos);
  if (!itemIndex(b, n, wrap, sel) || off >= 0.5f) return;
  const MenuItem &item = items[sel];

  sc.label = s_alert ? s_alert : item.on ? STOP_LABEL : item.label.c_str();
  const int textW = (int)strlen(sc.label) * TEXT_ADV;
  sc.clipL = l.cx - l.maxW / 2;
  sc.clipR = sc.clipL + l.maxW;
  if (textW <= l.maxW) {
    sc.labelX = jsRound(l.cx - textW / 2.0f);
  } else {
    sc.labelX = jsRound(sc.clipL - (textW - l.maxW) * marqueeProgress(now));
  }

  if (s_alert) {
    sc.layers[sc.nLayers++] = {0, 0, C_ALERT};
  } else {
    // Cyan below and pink to the left leave a coloured fringe on the text, and
    // fade out as the entry rolls away from the middle.
    const float strength = 1.0f - off * 2.0f;
    sc.layers[sc.nLayers++] = {0, 1, mix(C_UNDER, C_BOX, strength)};
    sc.layers[sc.nLayers++] = {-2, 0, mix(C_PINK, C_BOX, strength)};
    sc.layers[sc.nLayers++] = {0, 0, item.on ? C_RUNNING : C_SELECTED};
  }
}

uint32_t hashScene(const Scene &sc) {
  const uint8_t *p = (const uint8_t *)&sc;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < sizeof sc; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

// ---- Painting --------------------------------------------------------------

bool touches(const StripCanvas &g, int y, int h) { return y < g.bottom() && y + h > g.top(); }

void paintRuler(StripCanvas &g, const Layout &l, int16_t off) {
  const int cy = l.h / 2;
  const int r = off - cy;
  const int q = r >= 0 ? r / 5 : -((-r + 4) / 5);  // floor(r / 5)
  const int rem = r - q * 5;
  const int span = l.rulerLimit / 5 + 1;
  for (int m = -span; m <= span; m++) {
    const int dy = rem + 5 * m;
    if (abs(dy) > l.rulerLimit) continue;
    const bool major = ((q + m) & 1) == 0;
    const uint16_t color = mix(C_RULER, C_BG, 1.0f - (float)abs(dy) / l.rulerFade);
    g.fillRect(RULER_X, cy + dy, major ? 5 : 2, 1, color);
  }
  g.fillRect(RULER_X, cy - PTR_H / 2, PTR_W, PTR_H, C_PINK);
}

void paintPixelText(StripCanvas &g, const char *s, int16_t x, int16_t y, uint16_t color) {
  for (; *s; ++s, x += CARD_ADV) {
    if (x >= g.clipRight()) break;
    if (x + 5 <= g.clipLeft()) continue;
    int k = (unsigned char)*s - 32;
    if (k < 0 || k > 94) k = '?' - 32;
    for (int col = 0; col < 5; col++) {
      const uint8_t bits = FONT_5X7[k][col];
      if (!bits) continue;
      // Rows 1 and 2 share one output row.
      const uint8_t rows = (bits & 1) | ((((bits >> 1) | (bits >> 2)) & 1) << 1) | (((bits >> 3) & 0x0F) << 2);
      for (int row = 0; row < CARD_ROWS; row++) {
        if (rows & (1 << row)) g.drawPixel(x + col, y + row, color);
      }
    }
  }
}

void paintCard(StripCanvas &g, const CardView &c) {
  if (!touches(g, c.y, c.h)) return;
  g.fillRect(c.x, c.y, c.w, c.h, c.fill);
  g.fillRect(c.x, c.above ? c.y : c.y + c.h - CARD_EDGE, c.w, CARD_EDGE, c.outer);
  g.fillRect(c.x, c.above ? c.y + c.h - 1 : c.y, c.w, 1, c.inner);
  if (c.clipW) g.setClip(c.textX, c.textX + c.clipW);
  paintPixelText(g, c.label, c.textX, c.textY, c.text);
  g.clearClip();
}

// The columns [lo, hi] of pixel row y that the fill covers, inside the frame.
bool fillSpan(const Scene &sc, int y, int &lo, int &hi) {
  const int32_t dy = 2 * y - (2 * sc.frameY + sc.frameH);
  const int32_t room = sc.fillR2 - dy * dy;  // what is left of the radius squared
  if (room <= 0) return false;
  const int32_t cx2 = 2 * sc.frameX + sc.frameW;
  const int reach = isqrt(room - 1);  // 2 * |x - centre| can be at most this
  lo = imax(sc.frameX, (int)((cx2 - reach + 1) >> 1));
  hi = imin(sc.frameX + sc.frameW - 1, (int)((cx2 + reach) >> 1));
  return lo <= hi;
}

// Fill the part of pixel rows [y0, y1) that the disc covers, in this strip.
void fillRows(StripCanvas &g, const Scene &sc, int y0, int y1, uint16_t color) {
  for (int y = imax(y0, g.top()); y < imin(y1, g.bottom()); y++) {
    int lo, hi;
    if (fillSpan(sc, y, lo, hi)) g.fillRect(lo, y, hi - lo + 1, 1, color);
  }
}

// The label's layers, cut to the label window.
void paintLayers(StripCanvas &g, const Scene &sc, const Layout &l,
                 const TextLayer *layers, uint8_t count) {
  g.setClip(sc.clipL, sc.clipR);
  for (uint8_t i = 0; i < count; i++) {
    int16_t x = sc.labelX + layers[i].dx;
    for (const char *p = sc.label; *p; ++p, x += TEXT_ADV) {
      if (x >= sc.clipR) break;
      if (x + TEXT_ADV <= sc.clipL) continue;
      g.drawChar(x, l.textBase + layers[i].dy, (unsigned char)*p, layers[i].color,
                 layers[i].color, 1);
    }
  }
  g.clearClip();
}

// The label is drawn in two styles, split by the edge of the fill disc.
void paintLabel(StripCanvas &g, const Scene &sc, const Layout &l) {
  if (!sc.label || !touches(g, l.textBase - TEXT_ASCENT, TEXT_ASCENT + 4)) return;
  const int32_t cx2 = 2 * sc.frameX + sc.frameW, cy2 = 2 * sc.frameY + sc.frameH;
  g.setFont(&FreeMonoBold9pt7b);
  if (sc.fillR2 > 0) g.setDisc(cx2, cy2, sc.fillR2, false);
  paintLayers(g, sc, l, sc.layers, sc.nLayers);
  if (sc.fillR2 > 0) {
    g.setDisc(cx2, cy2, sc.fillR2, true);
    paintLayers(g, sc, l, FILLED_LAYERS, 3);
  }
  g.clearDisc();
  g.setFont(nullptr);
}

void paintFrameEdges(StripCanvas &g, const Scene &sc) {
  const int16_t x = sc.frameX, y = sc.frameY, w = sc.frameW, h = sc.frameH;
  const int16_t r = x + w - 1, b = y + h - 1;
  // Pink, turning white where the fill has reached.
  g.fillRect(x, y, w, RAIL_THICK, C_PINK);
  g.fillRect(x, b - RAIL_THICK + 1, w, RAIL_THICK, C_PINK);
  fillRows(g, sc, y, y + RAIL_THICK, C_PRESS_LINE);
  fillRows(g, sc, b - RAIL_THICK + 1, b + 1, C_PRESS_LINE);
  const int16_t arm = CORNER_ARM, thick = CORNER_THICK;
  g.fillRect(x, y, arm, thick, C_EDGE);
  g.fillRect(x, y, thick, arm, C_EDGE);
  g.fillRect(r - arm + 1, y, arm, thick, C_EDGE);
  g.fillRect(r - thick + 1, y, thick, arm, C_EDGE);
  g.fillRect(x, b - thick + 1, arm, thick, C_EDGE);
  g.fillRect(x, b - arm + 1, thick, arm, C_EDGE);
  g.fillRect(r - arm + 1, b - thick + 1, arm, thick, C_EDGE);
  g.fillRect(r - thick + 1, b - arm + 1, thick, arm, C_EDGE);
}

void paintStrip(StripCanvas &g, const Scene &sc, const Layout &l) {
  g.clear(C_BG);
  for (uint8_t i = 0; i < sc.nCards; i++) paintCard(g, sc.cards[i]);
  if (touches(g, sc.frameY, sc.frameH)) {
    g.fillRect(sc.frameX, sc.frameY, sc.frameW, sc.frameH, C_BOX);
    fillRows(g, sc, sc.frameY, sc.frameY + sc.frameH, C_PRESS_FILL);
    paintLabel(g, sc, l);
    paintFrameEdges(g, sc);
  }
  paintRuler(g, l, sc.rulerOff);
}

// Draw the strips that overlap rows [y0, y1) and push each to the panel.
void render(Adafruit_SPITFT &panel, const Layout &l, const Scene &sc, int16_t y0, int16_t y1) {
  s_canvas.configure(l);
  panel.startWrite();
  for (int16_t top = 0; top < l.h; top += l.stripH) {
    if (top + l.stripH <= y0 || top >= y1) continue;
    s_canvas.setStrip(top);
    paintStrip(s_canvas, sc, l);
    panel.setAddrWindow(0, top, l.w, l.stripH);
    panel.writePixels(s_stripBuf, (uint32_t)l.w * l.stripH);
  }
  panel.endWrite();
}

// Paint the scene if it looks different from the last one drawn. A list that
// has not moved only ever changes inside the frame (label, fill), so then only
// the strips that touch the frame are pushed.
void draw(Adafruit_SPITFT &panel, const Layout &l, uint32_t now, bool force) {
  Scene sc;
  buildScene(sc, l, now);
  const uint32_t hash = hashScene(sc);
  if (!force && s_haveDrawn && hash == s_drawnHash) {
    s_drawnPos = s_pos;  // the picture already on the panel is this position's
    return;
  }

  const bool all = force || !s_haveDrawn || s_pos != s_drawnPos;
  render(panel, l, sc, all ? 0 : sc.frameY, all ? l.h : sc.frameY + sc.frameH);
  s_haveDrawn = true;
  s_drawnHash = hash;
  s_drawnPos = s_pos;
}

}  // namespace

// ---- Public API ------------------------------------------------------------

void menuViewShow(Adafruit_SPITFT &panel, bool landscape,
                  const std::vector<MenuItem> &items, size_t selected,
                  const char *alert) {
  const uint32_t now = millis();
  const size_t n = items.size();
  const size_t sel = selected < n ? selected : 0;
  const bool continuing = s_active && landscape == s_landscape && s_count == n && n > 0;
  const bool wrap = n >= (size_t)WRAP_MIN;
  const bool stepped = continuing && n > 1 && sel == (s_selected + 1) % n && (wrap || sel > s_selected);
  const bool sameAlert = (alert == nullptr && s_alert == nullptr) ||
                         (alert != nullptr && s_alert != nullptr && strcmp(alert, s_alert) == 0);

  s_items = &items;
  s_landscape = landscape;
  s_count = n;
  s_selected = sel;
  s_alert = alert;
  s_active = true;
  if (stepped) {
    s_target++;  // taps in quick succession pile up and the list keeps rolling
  } else {
    s_target = (int32_t)sel;
    s_pos = (float)sel;
  }
  if (stepped || !continuing || !sameAlert) s_selectedAt = now;
  s_animAt = now;
  s_nextFrame = now + FRAME_MS;
  if (n == 0) return;
  draw(panel, layoutFor(landscape), now, true);
}

void menuViewHold(uint32_t heldMs) {
  const bool filled = heldMs >= MENU_HOLD_MS && heldMs < SCREEN_LOCK_ENTER_HOLD_MS;
  if (s_filled && heldMs == 0) s_flashUntil = millis() + FLASH_MS;
  s_filled = filled;
  s_heldMs = heldMs;
}

void menuViewTick(Adafruit_SPITFT &panel, bool landscape) {
  if (!s_active || !s_items || s_items->empty() || landscape != s_landscape) return;
  const uint32_t now = millis();
  if ((int32_t)(now - s_nextFrame) < 0) return;
  s_nextFrame = now + FRAME_MS;

  float dt = (now - s_animAt) / 1000.0f;
  s_animAt = now;
  if (dt > MAX_DT) dt = MAX_DT;
  const float goal = (float)s_target;
  if (s_pos != goal) {
    s_pos += (goal - s_pos) * (1.0f - expf(-dt * ROLL_RATE));
    if (fabsf(goal - s_pos) < ROLL_SNAP) {
      // Arrived. Fold the position back so a long session does not drift.
      const int32_t n = (int32_t)s_items->size();
      s_target %= n;
      s_pos = (float)s_target;
    }
  }
  draw(panel, layoutFor(landscape), now, false);
}

void menuViewHide() {
  s_active = false;
  s_items = nullptr;
  s_alert = nullptr;
  s_filled = false;
  s_heldMs = 0;
  s_flashUntil = 0;
  s_haveDrawn = false;
}
