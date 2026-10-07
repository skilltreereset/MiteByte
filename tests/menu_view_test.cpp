// Runs the production menu view against a fake panel that remembers what was
// pushed to it: the picture, how many strips it took, and whether the view
// touched the heap while running.
#include "config.h"
#include "display_color.h"
#include "menu_view.h"
#include <Adafruit_SPITFT.h>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

static uint32_t now = 1000;
uint32_t millis() { return now; }

// Count heap allocations while armed.
static bool countAllocs = false;
static int allocs = 0;
void *operator new(std::size_t n) {
  if (countAllocs) ++allocs;
  if (void *p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }

static constexpr uint16_t BG = rgb(0x00, 0x00, 0x00);
static constexpr uint16_t BOX = rgb(0x05, 0x0D, 0x1F);
static constexpr uint16_t PINK = rgb(0xFF, 0x3D, 0x9A);
static constexpr uint16_t UNDER = rgb(0x19, 0xE6, 0xFF);
static constexpr uint16_t WHITE = rgb(0xFF, 0xFF, 0xFF);
// The menu's pink is also the fill, a running tool, STOP and the alerts.
static constexpr uint16_t PRESS = PINK;
static constexpr uint16_t ALERT = PINK;
static constexpr uint16_t STOP = PINK;
static constexpr uint16_t EDGE = rgb(0x7A, 0xD4, 0xFB);

static constexpr uint32_t FULL_LANDSCAPE = 160 * 80;
static constexpr uint32_t STRIP = 1280;  // pixels per strip, either orientation

static std::vector<MenuItem> sample() {
  return {{"SW-Monitor-Hardware", false}, {"Wi-Fi Hotspot", false}, {"Light", false},
          {"< BACK", false},              {"B00-test-layout", false},
          {"B01-command-reference", false}, {"B10-windows-notepad", false}};
}

static void tick(Adafruit_SPITFT &p, bool landscape, uint32_t ms = 33) {
  now += ms;
  menuViewTick(p, landscape);
}

// Tick until the view stops pushing; returns how many frames it drew.
static int settle(Adafruit_SPITFT &p, bool landscape = true) {
  int frames = 0;
  for (int quiet = 0; quiet < 5 && frames < 200;) {
    p.resetCounters();
    tick(p, landscape);
    if (p.pushes) { ++frames; quiet = 0; } else { ++quiet; }
  }
  return frames;
}

static bool sameScreen(const Adafruit_SPITFT &a, const Adafruit_SPITFT &b, int w, int h) {
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      if (a.pixel(x, y) != b.pixel(x, y)) return false;
  return true;
}

static int countColour(const Adafruit_SPITFT &p, uint16_t colour, int x0, int y0, int x1, int y1) {
  int n = 0;
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) n += p.pixel(x, y) == colour;
  return n;
}

int main() {
  const std::vector<MenuItem> items = sample();

  // ---- Landscape: the first frame is the whole screen, strip by strip ----
  Adafruit_SPITFT panel;
  menuViewShow(panel, true, items, 2, nullptr);
  assert(panel.pushes == 10 && panel.pixelsPushed == FULL_LANDSCAPE);

  // The selection frame: pink rails 2 px thick with blue corners, a dark box inside.
  assert(panel.pixel(20, 29) == PINK && panel.pixel(20, 30) == PINK && panel.pixel(20, 31) == BOX);
  assert(panel.pixel(20, 51) == PINK && panel.pixel(20, 50) == PINK && panel.pixel(20, 49) == BOX);
  // The corner brackets are 5 long and 2 thick, the second row and column
  // reaching in over the box.
  assert(panel.pixel(10, 29) == EDGE && panel.pixel(14, 29) == EDGE && panel.pixel(10, 33) == EDGE);
  assert(panel.pixel(10, 30) == EDGE && panel.pixel(14, 30) == EDGE && panel.pixel(11, 33) == EDGE);
  assert(panel.pixel(15, 30) == PINK && panel.pixel(12, 30) == EDGE && panel.pixel(12, 34) == BOX);
  assert(panel.pixel(157, 51) == EDGE && panel.pixel(157, 47) == EDGE && panel.pixel(156, 47) == EDGE);
  assert(panel.pixel(157, 50) == EDGE && panel.pixel(153, 50) == EDGE && panel.pixel(152, 50) == PINK);
  assert(panel.pixel(12, 35) == BOX);
  // The ruler pointer sits on the middle row, left of the frame.
  assert(panel.pixel(1, 39) == PINK && panel.pixel(7, 40) == PINK && panel.pixel(8, 39) == BG);
  // "Light" is centred, its fourth glyph spanning x 90..98 on rows 35..45. It
  // shows white, with the cyan fringe one row below and the pink one 2 px left.
  assert(panel.pixel(94, 40) == WHITE);
  assert(panel.pixel(95, 46) == UNDER);
  assert(panel.pixel(88, 40) == PINK);

  // Nothing is repainted while nothing changes.
  panel.resetCounters();
  menuViewTick(panel, true);
  tick(panel, true);
  tick(panel, true, 500);
  assert(panel.pushes == 0);

  // ---- Rolling ----
  menuViewShow(panel, true, items, 3, nullptr);
  assert(panel.pixel(20, 26) == PINK && panel.pixel(20, 54) == PINK);  // frame grown by 6
  int frames = settle(panel);
  assert(frames > 3 && frames < 40);
  {
    Adafruit_SPITFT fresh;
    menuViewHide();
    menuViewShow(fresh, true, items, 3, nullptr);
    assert(sameScreen(panel, fresh, 160, 80));  // settled state is the direct draw
  }

  // Taps that arrive faster than the roll pile up and the list keeps going.
  menuViewShow(panel, true, items, 4, nullptr);
  menuViewShow(panel, true, items, 5, nullptr);
  menuViewShow(panel, true, items, 6, nullptr);
  settle(panel);
  {
    Adafruit_SPITFT fresh;
    menuViewHide();
    menuViewShow(fresh, true, items, 6, nullptr);
    assert(sameScreen(panel, fresh, 160, 80));
  }
  // ... and wrapping from the last entry to the first rolls on round.
  menuViewShow(panel, true, items, 0, nullptr);
  assert(panel.pixel(20, 26) == PINK);
  settle(panel);
  {
    Adafruit_SPITFT fresh;
    menuViewHide();
    menuViewShow(fresh, true, items, 0, nullptr);
    assert(sameScreen(panel, fresh, 160, 80));
  }

  // ---- A label too long for the frame slides, and only the frame repaints ----
  menuViewShow(panel, true, items, 6, nullptr);
  settle(panel);
  uint32_t marqueePixels = 0;
  int marqueeFrames = 0;
  for (int i = 0; i < 120; i++) {
    panel.resetCounters();
    tick(panel, true);
    if (panel.pushes) {
      marqueePixels = panel.pixelsPushed;
      ++marqueeFrames;
      assert(panel.pushes <= 4 && panel.pixelsPushed <= 4 * STRIP);  // strips 24..55
    }
  }
  assert(marqueeFrames > 5 && marqueePixels > 0);

  // ---- Holding fills the frame; releasing flashes, then it clears ----
  menuViewShow(panel, true, items, 2, nullptr);
  settle(panel);
  // A tap-length press draws nothing; a longer one grows a pink disc from the
  // middle of the frame, 150 ms in, and it covers the frame when the hold is
  // long enough to select.
  menuViewHold(149);
  panel.resetCounters();
  tick(panel, true);
  assert(panel.pushes == 0 && panel.pixel(84, 32) == BOX);
  // 218 ms is 27% of the way: a disc about 20.6 px across the frame's middle (84, 40.5).
  menuViewHold(218);
  panel.resetCounters();
  tick(panel, true);
  assert(panel.pushes == 4);  // only the strips the frame touches
  assert(panel.pixel(84, 32) == PRESS && panel.pixel(102, 32) == PRESS);
  assert(panel.pixel(103, 32) == BOX && panel.pixel(12, 32) == BOX && panel.pixel(140, 32) == BOX);
  assert(panel.pixel(84, 29) == WHITE && panel.pixel(84, 30) == WHITE);  // rails: white inside the disc
  assert(panel.pixel(20, 29) == PINK && panel.pixel(140, 29) == PINK);   // and pink outside it
  // The label changes style at the disc's edge: two shadow rows inside it, one outside.
  assert(panel.pixel(95, 47) == UNDER && panel.pixel(95, 46) == UNDER);
  assert(panel.pixel(60, 47) == BOX && panel.pixel(60, 46) == UNDER);
  menuViewHold(MENU_HOLD_MS);
  panel.resetCounters();
  tick(panel, true);
  assert(panel.pushes == 4 && panel.pixel(12, 32) == PRESS && panel.pixel(150, 49) == PRESS);
  assert(panel.pixel(20, 29) == WHITE && panel.pixel(20, 30) == WHITE);  // rails turn white
  assert(panel.pixel(150, 50) == WHITE && panel.pixel(150, 51) == WHITE);
  assert(panel.pixel(95, 46) == UNDER && panel.pixel(95, 47) == UNDER);  // 2 px cyan shadow
  menuViewHold(SCREEN_LOCK_ENTER_HOLD_MS - 1);
  panel.resetCounters();
  tick(panel, true);
  assert(panel.pixel(12, 35) == PRESS);
  menuViewHold(0);  // released: the fill lingers
  tick(panel, true, 100);
  assert(panel.pixel(12, 35) == PRESS);
  tick(panel, true, 250);
  assert(panel.pixel(12, 35) == BOX && panel.pixel(20, 29) == PINK);
  // Held on into the screen lock: no fill, and no flash on the way out.
  menuViewHold(MENU_HOLD_MS + 10);
  tick(panel, true);
  menuViewHold(SCREEN_LOCK_ENTER_HOLD_MS);
  tick(panel, true);
  assert(panel.pixel(12, 35) == BOX);
  menuViewHold(0);
  tick(panel, true, 100);
  assert(panel.pixel(12, 35) == BOX);

  // ---- An alert replaces the label in red, without the fringe ----
  menuViewShow(panel, true, items, 2, "BUSY");  // 4 glyphs, the first at x 62..70
  assert(panel.pixel(66, 40) == ALERT && panel.pixel(66, 46) == BOX);
  menuViewShow(panel, true, items, 2, nullptr);
  assert(panel.pixel(94, 40) == WHITE);

  // ---- A running tool: its card is pink; selected, it reads STOP ----
  {
    std::vector<MenuItem> tools = sample();
    // Entry 0 is the card two above the selection; entry 2 is the selection.
    menuViewShow(panel, true, tools, 2, nullptr);
    // Rows 3..13 of the card two above the selection hold its text, clear of
    // the card's own pink edge on row 1.
    assert(countColour(panel, PINK, 19, 3, 149, 14) == 0);
    // Cards have a two-row pink edge on the side away from the frame (rows 1 and
    // 2 of the card two above), and a one-row light edge towards it (row 14).
    assert(panel.pixel(30, 1) != BG && panel.pixel(30, 2) == panel.pixel(30, 1));
    assert(panel.pixel(30, 3) != panel.pixel(30, 2) && panel.pixel(30, 14) != panel.pixel(30, 13));
    tools[0].on = true;
    menuViewShow(panel, true, tools, 2, nullptr);
    assert(countColour(panel, PINK, 19, 3, 149, 14) > 10);
    tools[2].on = true;
    menuViewShow(panel, true, tools, 2, nullptr);
    // "STOP" is 4 glyphs, the first at x 62..70, in place of "Light" (x 57..65).
    assert(panel.pixel(66, 40) == STOP && panel.pixel(58, 40) != WHITE);
    menuViewHold(MENU_HOLD_MS);  // pressed: the stop confirmation is white on pink
    tick(panel, true);
    assert(panel.pixel(66, 40) == WHITE && panel.pixel(12, 35) == PRESS);
    menuViewHold(0);
    menuViewHide();
  }

  // ---- Portrait: the same parts on 80x160 ----
  {
    Adafruit_SPITFT tall;
    menuViewHide();
    menuViewShow(tall, false, items, 2, nullptr);
    assert(tall.pushes == 10 && tall.pixelsPushed == 80 * 160);
    assert(tall.pixel(30, 69) == PINK && tall.pixel(10, 69) == EDGE && tall.pixel(77, 91) == EDGE);
    assert(tall.pixel(1, 79) == PINK && tall.pixel(7, 80) == PINK);
    settle(tall, false);
    // A mismatched orientation is ignored rather than drawn on the wrong grid.
    tall.resetCounters();
    tick(tall, true);
    assert(tall.pushes == 0);
    // The card just above the frame is solid, neither background nor frame box.
    assert(tall.pixel(15, 60) != BG && tall.pixel(15, 60) != BOX);
  }

  // ---- A list too short to wrap does not roll from the last entry to the first ----
  {
    const std::vector<MenuItem> few = {{"BACK", false}, {"One", false}, {"Two", false}};
    Adafruit_SPITFT p;
    menuViewHide();
    menuViewShow(p, true, few, 1, nullptr);
    menuViewShow(p, true, few, 2, nullptr);
    settle(p);
    menuViewShow(p, true, few, 0, nullptr);  // wraps: snaps
    p.resetCounters();
    tick(p, true);
    assert(p.pushes == 0);
  }

  // ---- Nothing to show, or not showing: nothing is drawn ----
  {
    const std::vector<MenuItem> none;
    Adafruit_SPITFT p;
    menuViewHide();
    menuViewShow(p, true, none, 0, nullptr);
    tick(p, true);
    assert(p.pushes == 0);
    menuViewShow(p, true, items, 0, nullptr);
    menuViewHide();
    p.resetCounters();
    tick(p, true);
    assert(p.pushes == 0);
    // The list emptying under an open menu leaves the last picture alone.
    std::vector<MenuItem> shrinking = sample();
    menuViewShow(p, true, shrinking, 2, nullptr);
    shrinking.clear();
    p.resetCounters();
    tick(p, true);
    tick(p, true, 700);
    assert(p.pushes == 0);
  }

  // ---- No heap while the menu is up ----
  {
    Adafruit_SPITFT p;
    menuViewHide();
    countAllocs = true;
    allocs = 0;
    for (size_t sel = 0; sel < items.size(); sel++) {
      menuViewShow(p, true, items, sel, sel == 3 ? "BUSY" : nullptr);
      for (int i = 0; i < 60; i++) tick(p, true);
    }
    menuViewHold(MENU_HOLD_MS);
    tick(p, true);
    menuViewHold(0);
    tick(p, true, 400);
    menuViewHide();
    countAllocs = false;
    assert(allocs == 0);
  }

  std::cout << "menu_view_test ok\n";
  return 0;
}
