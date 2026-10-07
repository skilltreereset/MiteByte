#pragma once
#include <Arduino.h>
#include <vector>

class Adafruit_SPITFT;

// The button menu as a vertical list: the selection sits in a frame in the
// middle, its neighbours stack away as cards on either side, and a ruler with a
// pointer runs down the left edge. Tapping rolls the list one entry; holding
// fills the frame to confirm.
//
// The view owns no panel and no heap. It draws into a small static strip
// buffer and pushes the panel one strip at a time, so it costs 2.5 KiB of RAM
// for good rather than a screen-sized buffer while open.

struct MenuItem {
  String label;
  bool on;  // a running tool
};

// Show (or update) the menu. `items` is read by reference on every frame and
// must outlive the view, as must `alert`, which is a string literal such as
// "BUSY" that replaces the selected label in red until the next call. A change
// of one step forward from the last call rolls; anything else snaps.
void menuViewShow(Adafruit_SPITFT &panel, bool landscape,
                  const std::vector<MenuItem> &items, size_t selected,
                  const char *alert);

// How long the button has been held, 0 when it is up. The frame fills once the
// hold is long enough to select, and flashes briefly after the release.
void menuViewHold(uint32_t heldMs);

// Advance the animation and draw a frame if anything changed. Cheap when idle.
void menuViewTick(Adafruit_SPITFT &panel, bool landscape);

// Forget the menu. The panel is left as it is; the caller repaints over it.
void menuViewHide();
