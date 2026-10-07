#pragma once
#include <Arduino.h>
#include "macro.h"
#include "menu_view.h"
#include <vector>

struct DisplayInfo {
  String ssid;
  String ip;
  String arrangement;
  String name;
  int clients;
  bool sdPresent;
  bool sdExposed;
  bool armed;   // a script is waiting to fire at the next boot
  MacroStatus macro;
  String toolState;
  bool toolError = false;
};

void displayBegin();

void displayShowJoin(const String &ssid, const String &password);

void displayShowMessage(const String &title, const String &detail);

void displayUpdate(const DisplayInfo &info);

// The button menu (see menu_view.h). Moving to the next entry rolls the list,
// animated by displayTick(). `items` is read by reference on every frame, so
// the caller keeps it alive while the menu is open; `alert` is a string
// literal that replaces the selected label in red until the next call. The
// next displayUpdate() replaces the menu, so callers stop updating the
// dashboard while it is open.
void displayShowMenu(const std::vector<MenuItem> &items, size_t selected,
                     const char *alert = nullptr);

// How long the button has been held, 0 when it is up: the menu fills its frame
// once the hold is long enough to select.
void displayMenuHold(uint32_t heldMs);

void displayTick();

void displaySetRotation(uint8_t rotation);
uint8_t displayGetRotation();

void displaySetScreenOn(bool on);
bool displayGetScreenOn();
// A lock overrides on/off, brightness and temporary wakes without changing
// the preference. UI changes made while locked apply when the lock is lifted.
void displaySetScreenLocked(bool locked);
// Immediately blank both lights; used when entering hard-lock standby.
void displayStandby();

void displaySetScreenBright(uint8_t percent);
void displaySetLedBright(uint8_t percent);

void displayWake();

// Flashlight: the whole panel white at full backlight, ignoring the screen
// on/off and brightness preferences. Locks still win. Tapping still shows
// the join screen, and the light returns once it times out.
void displaySetTorch(bool on);
bool displayGetTorch();

void displaySetLed(bool on);

// No device on the access point yet: the LED breathes red until one joins.
void displaySetWaiting(bool waiting);
