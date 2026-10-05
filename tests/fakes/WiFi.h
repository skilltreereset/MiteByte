#pragma once
#include "IPAddress.h"
struct FakeWiFi { IPAddress softAPIP() const { return IPAddress(0x0104a8c0); } };
inline FakeWiFi WiFi;
