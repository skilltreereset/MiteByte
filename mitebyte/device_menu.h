#pragma once
#include <Arduino.h>

// The on-device menu of scripts and tools, driven by the one button while
// ONLINE: a hold opens it, then a short press moves to the next entry and a
// hold runs a script or toggles a tool. It closes on BACK, after a script
// starts, or after SCREEN_WAKE_MS untouched.

bool menuIsOpen();
void menuOpen();
void menuClose();
void menuPress(bool hold);
// Returns true on the tick that closes the menu for being left idle.
bool menuTick();
