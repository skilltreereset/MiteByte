
#include "config.h"
#include "ducky.h"
#include "lock.h"
#include "storage.h"
#include "ui_display.h"
#include "usb_drive.h"
#include "usb_mode.h"
#include "web_api.h"
#include "tools.h"

#include "USB.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_mac.h>

#if ARDUINO_USB_MODE == 1
#error "Select USB Mode = USB-OTG (TinyUSB): hardware CDC mode cannot do HID."
#endif

static Settings g_settings;
static String g_ssid;
static String g_password;
static uint32_t g_nextRefresh = 0;
static uint32_t g_joinUntil = 0;
static bool g_joinLatched = true;

// From eFuse: WiFi.softAPmacAddress() returns zeros before softAP() runs.
static String defaultSsid() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  char suffix[8];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  return String(AP_SSID_PREFIX) + "-" + suffix;
}

// One shot: the snapshot is deleted before the payload is queued, so a crash
// or a replug mid-run cannot turn one arming into a payload that fires on
// every plug. Nothing is queued unless that delete actually took.
static void launchOnPlug(uint16_t startDelay) {
  if (storageArmedSize() == 0) return;

  String script = storageArmedRead();

  if (!storageArmedClear()) {
    duckyLog("== fire after boot: could not disarm, refusing to run ==");
    return;
  }
  if (script.isEmpty()) return;

  duckyLog("== fire after boot ==");
  duckyRun(script, "boot", startDelay);
}

// HID is brought up at most once, whether that is for an armed boot payload
// (while still locked) or on unlock.
static bool g_duckyBegun = false;
static void ensureDucky() {
  if (g_duckyBegun) return;
  g_duckyBegun = true;
  duckyBegin();
  duckySetLayout(g_settings.layout);
  // Register the event handler and initialize HID before exposing it to the
  // host, so its first keyboard reports are captured too.
  usbModeSetActive(true);
}

// A payload armed for the next boot fires here, while the dongle is still
// LOCKED: no unlock, no screen, no Wi-Fi. HID comes up only because a payload
// is waiting; an unarmed boot leaves it down. This is the documented "fire
// after boot", now working under the insertion lock.
static void launchArmedAtBoot() {
  if (storageArmedSize() == 0) return;
  ensureDucky();
  launchOnPlug(g_settings.startDelay);
}

// Everything else LOCKED holds back: the access point, the web interface and
// the normal screen. Called once on the LOCKED -> ONLINE edge.
static void goOnline() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(g_ssid.c_str(), g_password.c_str(), AP_CHANNEL, 0, AP_MAX_CLIENTS);

  Serial.printf("AP %s -> http://%s\n", g_ssid.c_str(), WiFi.softAPIP().toString().c_str());

  if (MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
  }

  // Lift the lock independently of the saved screen preference.
  displaySetScreenOn(g_settings.screenOn);
  displaySetScreenLocked(false);
  displaySetLedBright(g_settings.ledBright);
  displaySetLed(g_settings.ledOn);
  displaySetWaiting(true);

  ensureDucky();

  if (storageWasFormatted()) {
    duckyLog("== filesystem was reformatted: payloads and settings lost ==");
  }

  webBegin(g_ssid);
  toolsOnline();

  // Left up until a device joins, unless the operator would rather not
  // leave the password and a scannable code on show. The button still
  // reveals them, which needs the dongle in hand.
  g_joinLatched = g_settings.showAccess;
  if (g_joinLatched) displayShowJoin(g_ssid, g_password);

  // Any armed payload already fired at boot (launchArmedAtBoot), which clears
  // the arming, so there is nothing left to launch here.
}

static void goStandby() {
  if (g_duckyBegun) duckyAbort();
  toolsOffline();
  usbModeSetActive(false);
  webEnd();
  MDNS.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  // Stay in this boot: restarting here would consume one of the configured
  // hard-lock reinsertions before the owner has actually replugged it.
}

void setup() {
  Serial.begin(115200);

  pinMode(BOOT_PIN, INPUT_PULLUP);
  displayBegin();

  if (!storageBegin()) {
    Serial.println("LittleFS unavailable: the payload library will be empty.");
  } else if (storageWasFormatted()) {
    Serial.println("LittleFS could not be mounted and was reformatted: "
                   "payloads and settings have been lost.");
  }

  // Exactly once per boot, before anything reads the lock state.
  bool hardlockPending = storageHardlockTick();

  g_settings = storageLoadSettings();
  g_ssid = g_settings.ssid.isEmpty() ? defaultSsid() : g_settings.ssid;
  g_password = g_settings.password.isEmpty() ? storageDefaultPassword() : g_settings.password;

  displaySetRotation(g_settings.rotation);
  displaySetScreenBright(g_settings.screenBright);
  displaySetScreenOn(g_settings.screenOn); // startup lock keeps it dark

  // USB begins with storage only. The startup policy below decides whether
  // to wait in standby, run an armed script quietly, or bring everything up.
  usbDriveBegin(g_settings.usbDrive, g_settings.deviceName);

  lockBegin(g_settings.unlockSequence, g_settings.hardlockSequence,
            g_settings.longPressMs, hardlockPending,
            g_settings.hardlockEnabled, g_settings.hardlockReinserts,
            g_settings.standbyOnBoot || storageArmedSize() > 0);
  USB.begin(); // also supports builds without automatic CDC/USB startup
  toolsBegin();

  // A payload armed for this boot fires now, still locked and dark. A pending
  // hard lock suppresses it: a hard-locked dongle is inert until recovered.
  if (!hardlockPending) launchArmedAtBoot();
  // An armed run remains quiet even with standby disabled. Only an ordinary
  // unarmed boot can start online automatically; hard lock always wins.
  if (!lockIsLocked()) goOnline();
}

void loop() {
  LockEvent ev = lockTick();  // every mode, every iteration

  switch (ev) {
    case LOCK_EVT_UNLOCKED:
      goOnline();
      break;
    case LOCK_EVT_HARD_LOCKED:
      goStandby();
      break;
    case LOCK_EVT_TAP_ONLINE: {
      // A short press while ONLINE pulls up the QR/credentials screen for a
      // few seconds. displayShowJoin() only repaints if it is not already the
      // screen on show, so repeated taps no longer re-clear and flicker it;
      // they just extend the window.
      displayWake();
      displayShowJoin(g_ssid, g_password);
      g_joinUntil = millis() + 8000;
      break;
    }
    default:
      break;
  }

  // LOCKED: nothing else runs, mass storage only. SCREEN_LOCK keeps the
  // radio, HID and web going below, same as fully ONLINE.
  if (lockIsLocked()) {
    delay(2);
    return;
  }

  webLoop();
  DuckyStatus payloadStatus = duckyGetStatus();
  toolsTick(payloadStatus.state == DUCKY_RUNNING || payloadStatus.state == DUCKY_ARMED);
  displayTick();

  uint32_t now = millis();
  if (now >= g_nextRefresh) {
    g_nextRefresh = now + 400;

    DisplayInfo info;
    info.ssid = g_ssid;
    info.ip = WiFi.softAPIP().toString();
    info.arrangement = duckyGetArrangement();
    info.name = usbDriveGetDeviceName();
    UsbDriveStatus drv = usbDriveGetStatus();
    info.sdPresent = drv.cardPresent;
    info.sdExposed = drv.exposed;
    info.armed = storageArmedSize() > 0;
    info.clients = WiFi.softAPgetStationNum();
    info.ducky = duckyGetStatus();
    if (const ToolPlugin *tool = toolsFind(toolsActiveId())) {
      ToolStatus status = toolsStatus(*tool);
      info.toolError = status.state == "error";
      info.toolState = info.toolError ? "TOOL FAULT" : status.state == "waiting" ? "TOOL WAIT" : "TOOL ON";
      info.sdExposed = false; // tool USB profiles do not expose mass storage
    }

    displaySetWaiting(info.clients == 0);

    // Only a joined device clears it, which is what the setting promises. A
    // run used to clear it too, but a web-triggered run already implies a
    // client, so the clause only ever fired for a payload armed at boot and
    // wiped the join screen the operator had asked to keep.
    if (info.clients > 0) g_joinLatched = false;

    if (!g_joinLatched && now >= g_joinUntil) displayUpdate(info);
  }

  delay(2);
}
