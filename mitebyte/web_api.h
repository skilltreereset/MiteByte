#pragma once
#include <Arduino.h>

void webBegin(const String &ssid);
void webLoop();
void webEnd();
// A networking tool can own port 53 while the normal web UI stays available.
void webSetCaptiveDns(bool enabled);
