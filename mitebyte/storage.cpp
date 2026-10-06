#include "storage.h"
#include "config.h"
#include "macro.h"
#include "tools.h"
#include "generated/windows_setup_script.h"
#include "generated/scripts.h"
#include <esp_mac.h>

#include <FS.h>
#include <LittleFS.h>
#include <algorithm>

// Default scripts live as files in mitebyte/src/scripts/ and are embedded by
// tools/build_helpers.py into generated/scripts.h (SEEDED_SCRIPTS).

bool storageNameIsValid(const String &name) {
  if (name.isEmpty() || name.length() > MAX_NAME_LEN) return false;
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }

  if (name == "." || name == "..") return false;
  return true;
}

static String pathFor(const String &name) {
  return String(SCRIPT_DIR) + "/" + name;
}

static bool s_formatted = false;
static std::vector<ScriptInfo> s_metaCache;
static bool s_metaCacheValid = false;

static void invalidateMetaCache() { s_metaCacheValid = false; }

bool storageWasFormatted() { return s_formatted; }

bool storageBegin() {

  if (!LittleFS.begin(false)) {
    if (!LittleFS.begin(true)) return false;
    s_formatted = true;
  }

  if (!LittleFS.exists(SCRIPT_DIR)) {
    LittleFS.mkdir(SCRIPT_DIR);
  }

  Settings s = storageLoadSettings();
  if (s.seedVersion < SCRIPT_SEED_VERSION) {
    bool seeded = true;
    for (const SeededScript &d : SEEDED_SCRIPTS) {
      if (!storageExists(d.name) && !storageWrite(d.name, d.body)) seeded = false;
    }
    // The hotspot setup script is generated from the reviewed Windows installer.
    if (!storageExists("13-windows-hotspot-setup.txt") &&
        !storageWrite("13-windows-hotspot-setup.txt", WINDOWS_SHARING_SCRIPT)) seeded = false;
    if (seeded) {
      s.seedVersion = SCRIPT_SEED_VERSION;
      storageSaveSettings(s);
    }
  }
  return true;
}

std::vector<String> storageList() {
  std::vector<String> out;
  File dir = LittleFS.open(SCRIPT_DIR);
  if (!dir || !dir.isDirectory()) return out;

  File f = dir.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String n = String(f.name());
      int slash = n.lastIndexOf('/');
      if (slash >= 0) n = n.substring(slash + 1);
      out.push_back(n);
    }
    f = dir.openNextFile();
  }
  std::sort(out.begin(), out.end(), [](const String &a, const String &b) {
    return strcmp(a.c_str(), b.c_str()) < 0;
  });
  return out;
}

bool storageExists(const String &name) {
  if (!storageNameIsValid(name)) return false;
  return LittleFS.exists(pathFor(name));
}

struct OsAlias {
  const char *word;
  const char *os;
};

static const OsAlias OS_ALIASES[] = {
  {"win", "windows"},   {"windows", "windows"}, {"win10", "windows"},
  {"win11", "windows"}, {"msw", "windows"},
  {"lin", "linux"},     {"linux", "linux"},     {"gnu", "linux"},
  {"ubuntu", "linux"},  {"debian", "linux"},    {"gnome", "linux"},
  {"kde", "linux"},     {"kali", "linux"},
  {"mac", "macos"},     {"macos", "macos"},     {"osx", "macos"},
  {"darwin", "macos"},  {"apple", "macos"},
};

static String osTagFor(const String &name) {
  File f = LittleFS.open(pathFor(name), "r");
  if (!f) return String();

  String head;
  size_t budget = 512;
  while (f.available() && budget--) head += (char)f.read();
  f.close();

  int pos = 0;
  while (pos < (int)head.length()) {
    int nl = head.indexOf('\n', pos);
    String line = (nl < 0) ? head.substring(pos) : head.substring(pos, nl);
    pos = (nl < 0) ? head.length() : nl + 1;
    line.replace("\r", "");
    line.trim();
    if (line.isEmpty()) continue;

    String upper = line;
    upper.toUpperCase();
    if (!upper.startsWith("META")) continue;

    String tags = line.substring(4);
    tags.toLowerCase();
    tags.replace(",", " ");
    int tp = 0;
    while (tp < (int)tags.length()) {
      while (tp < (int)tags.length() && tags[tp] == ' ') tp++;
      int start = tp;
      while (tp < (int)tags.length() && tags[tp] != ' ') tp++;
      if (tp == start) break;
      String word = tags.substring(start, tp);
      for (const OsAlias &a : OS_ALIASES) {
        if (word == a.word) return String(a.os);
      }
    }
  }
  return String();
}

const std::vector<ScriptInfo> &storageListDetailed() {
  if (s_metaCacheValid) return s_metaCache;
  s_metaCache.clear();
  for (const String &n : storageList()) {
    s_metaCache.push_back({n, osTagFor(n)});
  }
  s_metaCacheValid = true;
  return s_metaCache;
}

String storageRead(const String &name) {
  if (!storageNameIsValid(name)) return String();
  File f = LittleFS.open(pathFor(name), "r");
  if (!f) return String();
  String content = f.readString();
  f.close();
  return content;
}

bool storageWrite(const String &name, const String &content) {
  invalidateMetaCache();
  if (!storageNameIsValid(name)) return false;
  if (content.length() > MAX_SCRIPT_BYTES) return false;
  File f = LittleFS.open(pathFor(name), "w");
  if (!f) return false;
  size_t written = f.print(content);
  f.close();
  return written == content.length();
}

bool storageDelete(const String &name) {
  invalidateMetaCache();
  if (!storageNameIsValid(name)) return false;
  return LittleFS.remove(pathFor(name));
}

String storageDefaultPassword() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  char suffix[9];
  snprintf(suffix, sizeof(suffix), "%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
  return String(AP_PASSWORD_PREFIX) + suffix;
}

bool storageDeviceNameIsValid(const String &name) {
  if (name.isEmpty() || name.length() > DEVICE_NAME_MAX) return false;

  for (size_t i = 0; i < name.length(); i++) {
    uint8_t c = (uint8_t)name[i];
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

bool storageSsidIsValid(const String &ssid) {
  if (ssid.length() < SSID_MIN_LEN || ssid.length() > SSID_MAX_LEN) return false;

  for (size_t i = 0; i < ssid.length(); i++) {
    if ((uint8_t)ssid[i] < 0x20 || (uint8_t)ssid[i] == 0x7F) return false;
  }
  return true;
}

bool storagePasswordIsValid(const String &password) {
  if (password.length() < PASSWORD_MIN_LEN || password.length() > PASSWORD_MAX_LEN) return false;

  for (size_t i = 0; i < password.length(); i++) {
    uint8_t c = (uint8_t)password[i];
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

Settings storageLoadSettings() {
  Settings s;
  s.layout = "us";
  s.rotation = TFT_ROTATION;
  s.screenOn = true;
  s.ledOn = true;
  s.screenBright = SCREEN_BRIGHTNESS_DEFAULT;
  s.ledBright = LED_BRIGHTNESS_DEFAULT;
  s.startDelay = 0;
  s.seedVersion = 0;
  s.usbDrive = false;
  s.showAccess = true;
  s.standbyOnBoot = true;

  s.unlockSequence = DEFAULT_UNLOCK_SEQUENCE;
  s.hardlockSequence = DEFAULT_HARDLOCK_SEQUENCE;
  s.longPressMs = LOCK_LONG_PRESS_MS;
  s.hardlockEnabled = true;
  s.hardlockReinserts = HARDLOCK_REINSERTS_DEFAULT;

  File f = LittleFS.open(SETTINGS_FILE, "r");
  if (!f) return s;

  String content = f.readString();
  f.close();

  if (content.indexOf('=') < 0) {

    String v = content;
    v.trim();
    if (v == "us" || v == "fr") s.layout = v;
    return s;
  }

  int pos = 0;
  while (pos < (int)content.length()) {
    int nl = content.indexOf('\n', pos);
    String line = (nl < 0) ? content.substring(pos) : content.substring(pos, nl);
    pos = (nl < 0) ? content.length() : nl + 1;
    line.replace("\r", "");

    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    String key = line.substring(0, eq);
    String value = line.substring(eq + 1);
    key.trim();

    if (key == "layout") {
      value.trim();
      if (macroLayoutExists(value)) s.layout = value;
    } else if (key == "ssid") {

      if (storageSsidIsValid(value)) s.ssid = value;
    } else if (key == "password") {
      if (storagePasswordIsValid(value)) s.password = value;
    } else if (key == "rotation") {
      int r = value.toInt();
      if (r >= 0 && r <= 3) s.rotation = (uint8_t)r;
    } else if (key == "screen") {
      s.screenOn = (value.toInt() != 0);
    } else if (key == "led") {
      s.ledOn = (value.toInt() != 0);
    } else if (key == "screenbright") {
      long v = value.toInt();
      if (v >= BRIGHTNESS_MIN && v <= BRIGHTNESS_MAX) s.screenBright = (uint8_t)v;
    } else if (key == "ledbright") {
      long v = value.toInt();
      if (v >= BRIGHTNESS_MIN && v <= BRIGHTNESS_MAX) s.ledBright = (uint8_t)v;
    } else if (key == "devicename") {

      if (storageDeviceNameIsValid(value)) s.deviceName = value;
    } else if (key == "showaccess") {
      s.showAccess = (value.toInt() != 0);
    } else if (key == "standby") {
      s.standbyOnBoot = (value.toInt() != 0);
    } else if (key == "usbdrive") {
      s.usbDrive = (value.toInt() != 0);
    } else if (key == "seedversion") {
      long v = value.toInt();
      if (v >= 0 && v <= 65535) s.seedVersion = (uint16_t)v;
    } else if (key == "startdelay") {
      long d = value.toInt();
      if (d >= 0 && d <= START_DELAY_MAX) s.startDelay = (uint16_t)d;
    } else if (key == "unlockseq") {
      value.toUpperCase();
      if (storageLockSequenceIsValid(value)) s.unlockSequence = value;
    } else if (key == "hardlockseq") {
      value.toUpperCase();
      if (storageLockSequenceIsValid(value)) s.hardlockSequence = value;
    } else if (key == "longpressms") {
      long v = value.toInt();
      if (v >= 0 && v <= 65535 && storageLongPressIsValid((uint16_t)v)) s.longPressMs = (uint16_t)v;
    } else if (key == "hardlocken") {
      s.hardlockEnabled = (value.toInt() != 0);
    } else if (key == "hardlockre") {
      long v = value.toInt();
      if (v >= HARDLOCK_REINSERTS_MIN && v <= HARDLOCK_REINSERTS_MAX) {
        s.hardlockReinserts = (uint8_t)v;
      }
    }
  }
  // Recover settings written by older firmware that accepted conflicting
  // gestures, so an update cannot leave the owner unable to unlock.
  if (!storageLockSequencesAreCompatible(s.unlockSequence, s.hardlockSequence)) {
    s.unlockSequence = DEFAULT_UNLOCK_SEQUENCE;
    s.hardlockSequence = DEFAULT_HARDLOCK_SEQUENCE;
  }
  return s;
}

static size_t s_armedSize = 0;
static bool s_armedCached = false;

size_t storageArmedSize() {
  if (!s_armedCached) {
    File f = LittleFS.open(ARMED_FILE, "r");
    s_armedSize = f ? f.size() : 0;
    if (f) f.close();
    s_armedCached = true;
  }
  return s_armedSize;
}

bool storageArmedClear() {
  s_armedSize = 0;
  s_armedCached = true;
  return LittleFS.exists(ARMED_FILE) ? LittleFS.remove(ARMED_FILE) : true;
}

bool storageArmedWrite(const String &script) {
  if (script.isEmpty()) return storageArmedClear();
  if (script.length() > MAX_SCRIPT_BYTES) return false;

  File f = LittleFS.open(ARMED_FILE, "w");
  if (!f) return false;
  size_t written = f.print(script);
  f.close();

  if (written != script.length()) {
    storageArmedClear();
    return false;
  }
  s_armedSize = written;
  s_armedCached = true;
  return true;
}

String storageArmedRead() {
  File f = LittleFS.open(ARMED_FILE, "r");
  if (!f) return String();
  String content = f.readString();
  f.close();
  return content;
}

bool storageSaveSettings(const Settings &s) {
  if (!s.ssid.isEmpty() && !storageSsidIsValid(s.ssid)) return false;
  if (!s.password.isEmpty() && !storagePasswordIsValid(s.password)) return false;
  if (!storageLockSequencesAreCompatible(s.unlockSequence, s.hardlockSequence)) return false;
  if (!storageLongPressIsValid(s.longPressMs)) return false;

  File f = LittleFS.open(SETTINGS_FILE, "w");
  if (!f) return false;
  f.print("layout=");
  f.println(s.layout);
  if (!s.ssid.isEmpty()) {
    f.print("ssid=");
    f.println(s.ssid);
  }
  if (!s.password.isEmpty()) {
    f.print("password=");
    f.println(s.password);
  }
  f.print("rotation=");
  f.println(s.rotation);
  f.print("screen=");
  f.println(s.screenOn ? 1 : 0);
  f.print("led=");
  f.println(s.ledOn ? 1 : 0);
  f.print("screenbright=");
  f.println(s.screenBright);
  f.print("ledbright=");
  f.println(s.ledBright);
  f.print("startdelay=");
  f.println(s.startDelay);
  f.print("seedversion=");
  f.println(s.seedVersion);
  f.print("usbdrive=");
  f.println(s.usbDrive ? 1 : 0);
  f.print("showaccess=");
  f.println(s.showAccess ? 1 : 0);
  f.print("standby=");
  f.println(s.standbyOnBoot ? 1 : 0);
  if (!s.deviceName.isEmpty()) {
    f.print("devicename=");
    f.println(s.deviceName);
  }
  f.print("unlockseq=");
  f.println(s.unlockSequence);
  f.print("hardlockseq=");
  f.println(s.hardlockSequence);
  f.print("longpressms=");
  f.println(s.longPressMs);
  f.print("hardlocken=");
  f.println(s.hardlockEnabled ? 1 : 0);
  f.print("hardlockre=");
  f.println(s.hardlockReinserts);
  f.close();

  return true;
}

void storageResetSettings() {
  toolsReset();
  LittleFS.remove(SETTINGS_FILE);
  storageArmedClear();
  LittleFS.remove(HARDLOCK_FILE);
}

// See HARDLOCK_FILE in config.h: the file holds how many more boots, after
// this one, stay hard-locked. Called exactly once per boot, before anything
// reads the lock state.
bool storageHardlockTick() {
  File f = LittleFS.open(HARDLOCK_FILE, "r");
  long pending = f ? f.parseInt() : 0;
  if (f) f.close();

  if (pending <= 0) {
    LittleFS.remove(HARDLOCK_FILE);
    return false;
  }

  pending--;
  if (pending <= 0) {
    LittleFS.remove(HARDLOCK_FILE);
  } else {
    File w = LittleFS.open(HARDLOCK_FILE, "w");
    if (w) {
      w.print(pending);
      w.close();
    }
  }
  return true;
}

bool storageHardlockArm(uint8_t reinserts) {
  if (reinserts < HARDLOCK_REINSERTS_MIN) reinserts = HARDLOCK_REINSERTS_MIN;
  if (reinserts > HARDLOCK_REINSERTS_MAX) reinserts = HARDLOCK_REINSERTS_MAX;
  File f = LittleFS.open(HARDLOCK_FILE, "w");
  if (!f) return false;
  size_t written = f.print(reinserts); // this many subsequent boots stay locked
  f.close();
  return written == (reinserts < 10 ? 1u : 2u);
}

String storageToolStartupRead() {
  File f = LittleFS.open("/tool-startup.txt", "r");
  if (!f || f.size() > 64) return String();
  return f.readString();
}
bool storageToolStartupWrite(const String &id) {
  const char *path = "/tool-startup.txt", *pending = "/tool-startup.tmp";
  if (id.isEmpty()) return !LittleFS.exists(path) || LittleFS.remove(path);
  if (id.length() > 64) return false;
  File f = LittleFS.open(pending, "w");
  if (!f) return false;
  bool written = f.print(id) == id.length();
  f.close();
  if (written && LittleFS.rename(pending, path)) return true;
  LittleFS.remove(pending);
  return false;
}
