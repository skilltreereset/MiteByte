#pragma once
#include <Arduino.h>
#include "ducky.h"

struct DisplayInfo {
  String ssid;
  String ip;
  String arrangement;
  String name;
  int clients;
  bool sdPresent;
  bool sdExposed;
  bool armed;   // a script is waiting to fire at the next boot
  DuckyStatus ducky;
};

void displayBegin();

void displayShowJoin(const String &ssid, const String &password);

void displayShowMessage(const String &title, const String &detail);

void displayUpdate(const DisplayInfo &info);

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

void displaySetLed(bool on);

// No device on the access point yet: the LED breathes red until one joins.
void displaySetWaiting(bool waiting);
