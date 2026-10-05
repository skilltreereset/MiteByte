#pragma once
#include <Arduino.h>
#include <FS.h>
#include <vector>

struct UsbDriveStatus {
  bool cardPresent;
  bool exposed;
  uint64_t sizeMB;
};

void usbDriveBegin(bool exposed, const String &deviceName);

void usbDriveSetExposed(bool exposed);
// Retains the requested state even when identifying the card temporarily fails.
bool usbDriveExposureRequested();

UsbDriveStatus usbDriveGetStatus();

void usbDriveSetDeviceName(const String &name);
String usbDriveGetDeviceName();

struct SdEntry {
  String name;
  uint32_t size;
  bool isDir;
};

bool usbDriveFsAvailable();

bool usbDrivePathIsSafe(const String &path);

bool usbDriveList(const String &path, std::vector<SdEntry> &out);
File usbDriveOpen(const String &path);
bool usbDriveDelete(const String &path);
// One complete append/flush/close under the card ownership lock. Never writes
// while the host owns the volume. Two bounded diagnostic files are retained.
bool usbDriveDiagnosticAppend(const char *data, size_t size, bool newSession);
