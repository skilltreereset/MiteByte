#pragma once
#include <Arduino.h>
#include <vector>

bool storageBegin();

bool storageWasFormatted();

std::vector<String> storageList();

struct PayloadInfo {
  String name;
  String os;
};

const std::vector<PayloadInfo> &storageListDetailed();
bool storageExists(const String &name);
String storageRead(const String &name);
bool storageWrite(const String &name, const String &content);
bool storageDelete(const String &name);

bool storageNameIsValid(const String &name);

struct Settings {
  String layout;
  String ssid;
  String password;

  uint8_t rotation;
  bool screenOn;
  bool ledOn;
  uint8_t screenBright;
  uint8_t ledBright;

  uint16_t startDelay;
  uint16_t seedVersion;
  bool usbDrive;
  String deviceName;
  bool showAccess;
  bool standbyOnBoot;        // normal boots wait for the insertion gesture

  String unlockSequence;    // 'S'/'L' in order, e.g. "SSSLL"
  String hardlockSequence;  // same alphabet, e.g. "SSSSS"
  uint16_t longPressMs;
  bool hardlockEnabled;     // the hard-lock gesture does anything at all
  uint8_t hardlockReinserts;  // reinsertions that stay locked after arming
};

Settings storageLoadSettings();
bool storageSaveSettings(const Settings &s);

// The script armed for the next boot, snapshotted from the editor. Writing
// an empty one disarms. The size is cached, since /api/state reports the
// armed state every second and must not stat the file each time.
bool storageArmedWrite(const String &script);
bool storageArmedClear();
String storageArmedRead();
size_t storageArmedSize();

void storageResetSettings();

String storageDefaultPassword();

bool storageDeviceNameIsValid(const String &name);
bool storageSsidIsValid(const String &ssid);
bool storagePasswordIsValid(const String &password);

// A lock sequence: 1 to LOCK_SEQ_MAX_LEN characters, each 'S' or 'L'.
bool storageLockSequenceIsValid(const String &seq);
bool storageLockSequencesAreCompatible(const String &unlock, const String &hardlock);
bool storageLongPressIsValid(uint16_t ms);

// Hard-lock persistence, outside Settings so storageResetSettings() clears it
// outright. storageHardlockTick() runs once at boot: true means this boot
// stays hard-locked (and it has already counted the boot down), false means
// clear. storageHardlockArm() is called the instant the hard-lock gesture
// matches; it takes effect immediately without restarting.
bool storageHardlockTick();
bool storageHardlockArm(uint8_t reinserts);
