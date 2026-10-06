#include "screen_light.h"
#include "../../../ui_display.h"

// The display does the work; this only owns the on/off lifecycle.
static void begin() {}
static bool start(String &) { displaySetTorch(true); return true; }
static void stop() { displaySetTorch(false); }
static void tick() {}
static ToolStatus status() {
  bool on = displayGetTorch();
  return { on, on ? "on" : "stopped", on ? "Light on" : "Stopped", {} };
}

const ToolPlugin SCREEN_LIGHT_TOOL = {
  "screen-light", "Light", "White screen at full brightness.",
  begin, start, stop, tick, status,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};
