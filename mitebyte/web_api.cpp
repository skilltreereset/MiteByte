#include "web_api.h"
#include "config.h"
#include "macro.h"
#include "storage.h"
#include "web_assets.h"

#include "lock.h"
#include "ui_display.h"
#include "usb_drive.h"
#include "tools.h"

#include "captive_dns.h"
#include <WebServer.h>
#include <WiFi.h>

static WebServer server(80);
static CaptiveDns dns;
static String g_ssid;
static bool g_captiveDns = true;

static uint32_t g_rebootAt = 0;

static String jsonEscape(const String &in) {
  String out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((uint8_t)c < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", (uint8_t)c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

static const char *stateName(MacroState s) {
  switch (s) {
    case MACRO_ARMED:   return "armed";
    case MACRO_RUNNING: return "running";
    case MACRO_DONE:    return "done";
    case MACRO_ABORTED: return "aborted";
    case MACRO_ERROR:   return "error";
    default:            return "idle";
  }
}

static void sendJson(int code, const String &body) {
  server.send(code, "application/json; charset=utf-8", body);
}

static void sendError(int code, const String &msg) {
  sendJson(code, "{\"error\":\"" + jsonEscape(msg) + "\"}");
}

static void handleIndex() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", PAGE_INDEX);
}

static void handleState() {
  MacroStatus st = macroGetStatus();
  const std::vector<ScriptInfo> &items = storageListDetailed();

  // Polled once a second by every open page, and String grows in 16 byte
  // steps, so without this the response reallocates a few dozen times a
  // second for nothing.
  String json;
  json.reserve(384 + items.size() * (MAX_NAME_LEN + 40));

  json = "{";
  json += "\"ssid\":\"" + jsonEscape(g_ssid) + "\",";
  json += "\"version\":\"" FIRMWARE_VERSION "\",";
  json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"clients\":" + String(WiFi.softAPgetStationNum()) + ",";
  json += "\"layout\":\"" + macroGetLayout() + "\",";
  json += "\"arrangement\":\"" + macroGetArrangement() + "\",";
  json += "\"state\":\"" + String(stateName(st.state)) + "\",";
  json += "\"line\":" + String(st.line) + ",";
  json += "\"total\":" + String(st.total) + ",";
  json += "\"countdown\":" + String(st.countdown) + ",";
  json += "\"hostSeen\":" + String(macroHostSeen() ? 1 : 0) + ",";
  json += "\"armed\":" + String((uint32_t)storageArmedSize()) + ",";
  json += "\"screenLocked\":" + String(lockScreenIsLocked() ? 1 : 0) + ",";
  json += "\"message\":\"" + jsonEscape(st.message) + "\",";

  json += "\"scripts\":[";
  for (size_t i = 0; i < items.size(); i++) {
    if (i) json += ",";
    json += "{\"name\":\"" + jsonEscape(items[i].name) + "\",";
    json += "\"os\":\"" + jsonEscape(items[i].os) + "\"}";
  }
  json += "],\"activeTool\":\"" + jsonEscape(toolsActiveId()) + "\",";
  json += "\"startupTool\":\"" + jsonEscape(toolsStartupId()) + "\",\"tools\":[";
  for (size_t i = 0; i < toolsCount(); ++i) {
    const ToolPlugin *tool = toolsAt(i);
    ToolStatus status = toolsStatus(*tool);
    if (i) json += ",";
    json += "{\"id\":\"" + jsonEscape(tool->id) + "\",\"name\":\"" + jsonEscape(tool->title) + "\",";
    json += "\"description\":\"" + jsonEscape(tool->description) + "\",\"running\":" + String(status.running ? 1 : 0) + ",";
    json += "\"notice\":\"" + jsonEscape(tool->notice ? tool->notice : "") + "\",";
    json += "\"setupUrl\":\"" + (tool->setupContent ? String("/api/tools/setup?id=") + tool->id : String()) + "\",";
    json += "\"setupTitle\":\"" + jsonEscape(tool->setupTitle ? tool->setupTitle : "") + "\",";
    json += "\"setupHint\":\"" + jsonEscape(tool->setupHint ? tool->setupHint : "") + "\",";
    json += "\"setupManual\":\"" + jsonEscape(tool->setupManual ? tool->setupManual : "") + "\",";
    json += "\"setupScriptName\":\"" + jsonEscape(tool->setupScriptName ? tool->setupScriptName : "") + "\",";
    json += "\"state\":\"" + jsonEscape(status.state) + "\",\"message\":\"" + jsonEscape(status.message) + "\",\"details\":[";
    for (size_t d = 0; d < status.details.size(); ++d) {
      if (d) json += ",";
      json += "{\"label\":\"" + jsonEscape(status.details[d].label) + "\",\"value\":\"" + jsonEscape(status.details[d].value) + "\"}";
    }
    json += "]}";
  }
  json += "]}";

  sendJson(200, json);
}

static void handleScriptGet() {
  String name = server.arg("name");
  if (!storageExists(name)) {
    sendError(404, "Script not found");
    return;
  }
  server.send(200, "text/plain; charset=utf-8", storageRead(name));
}

static void handleScriptSave() {
  String name = server.arg("name");
  String content = server.arg("content");

  if (!storageNameIsValid(name)) {
    sendError(400, "Invalid name: letters, digits, dot, dash and underscore only");
    return;
  }
  if (content.length() > MAX_SCRIPT_BYTES) {
    sendError(413, "Script too large");
    return;
  }
  if (!storageWrite(name, content)) {
    sendError(500, "Could not write to flash");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

static void handleScriptDelete() {
  String name = server.arg("name");
  if (!storageDelete(name)) {
    sendError(404, "Could not delete");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

static void handleRun() {
  if (toolsRunning()) { sendError(409, "Stop the active tool before running a script"); return; }
  String script = server.arg("script");
  String name = server.arg("name");
  String origin = "editor";

  if (script.isEmpty() && !name.isEmpty()) {
    if (!storageExists(name)) {
      sendError(404, "Script not found");
      return;
    }
    script = storageRead(name);
    origin = name;
  }

  if (script.isEmpty()) {
    sendError(400, "Empty script");
    return;
  }

  Settings st = storageLoadSettings();
  uint16_t delay = st.startDelay;
  if (server.hasArg("delay")) {
    long d = server.arg("delay").toInt();
    if (d < 0 || d > START_DELAY_MAX) {
      sendError(400, "Delay must be between 0 and " + String(START_DELAY_MAX) + " seconds");
      return;
    }
    delay = (uint16_t)d;
    if (delay != st.startDelay) {
      st.startDelay = delay;
      st.layout = macroGetLayout();
      storageSaveSettings(st);
    }
  }

  if (!macroRun(script, origin, delay)) {
    sendError(409, "A script is already running");
    return;
  }
  sendJson(202, "{\"ok\":true}");
}

static void handleStop() {
  macroAbort();
  sendJson(200, "{\"ok\":true}");
}

static void handleToolStart() {
  String error;
  if (!toolsStart(server.arg("id"), error)) { sendError(409, error); return; }
  sendJson(202, "{\"ok\":true}");
}
static void handleToolStop() {
  String id = server.arg("id");
  if (!id.isEmpty() && id != toolsActiveId()) {
    if (!toolsFind(id)) { sendError(404, "Tool not found"); return; }
    sendJson(200, "{\"ok\":true}"); return;
  }
  toolsStop();
  sendJson(200, "{\"ok\":true}");
}
static void handleToolStartup() {
  if (!server.hasArg("id") || (!server.arg("id").isEmpty() && !toolsFind(server.arg("id")))) {
    sendError(400, "Unknown startup tool"); return;
  }
  if (!toolsSetStartup(server.arg("id"))) { sendError(500, "Could not save startup tool"); return; }
  sendJson(200, "{\"ok\":true}");
}
static void handleToolSetup() {
  const ToolPlugin *tool = toolsFind(server.arg("id"));
  if (!tool || !tool->setupContent) { sendError(404, "Setup file not found"); return; }
  server.sendHeader("Content-Disposition", String("attachment; filename=\"") + tool->setupName + "\"");
  server.send_P(200, "application/octet-stream", tool->setupContent);
}

// Arms the script as it stands, not a reference to a library entry: the
// operator edits in place and arms what they see. An empty script disarms.
static void handleLaunchOnPlug() {
  if (!server.hasArg("script")) {
    sendError(400, "Missing script; send an empty one to disarm");
    return;
  }

  String script = server.arg("script");
  if (script.length() > MAX_SCRIPT_BYTES) {
    sendError(400, "Script is larger than " + String(MAX_SCRIPT_BYTES) + " bytes");
    return;
  }

  if (!storageArmedWrite(script)) {
    sendError(500, "Could not write to flash");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

static void handleLayout() {
  String layout = server.arg("layout");
  if (!macroSetLayout(layout)) {
    sendError(400, "Unknown layout code");
    return;
  }

  Settings st = storageLoadSettings();
  st.layout = macroGetLayout();
  storageSaveSettings(st);
  sendJson(200, "{\"ok\":true}");
}

static void handleLog() {
  uint32_t since = server.hasArg("since") ? (uint32_t)strtoul(server.arg("since").c_str(), nullptr, 10) : 0;
  uint32_t seq = 0;
  String text = macroGetLogSince(since, seq);
  sendJson(200, "{\"seq\":" + String(seq) + ",\"text\":\"" + jsonEscape(text) + "\"}");
}

static void handleLogClear() {
  uint32_t seq = macroClearLog();
  sendJson(200, "{\"ok\":true,\"seq\":" + String(seq) + "}");
}

static void handleSettingsGet() {
  Settings s = storageLoadSettings();

  String json;
  json.reserve(768 + macroLayoutCount() * 64);

  json = "{";
  json += "\"layout\":\"" + jsonEscape(macroGetLayout()) + "\",";
  json += "\"ssid\":\"" + jsonEscape(g_ssid) + "\",";
  json += "\"password\":\"" + jsonEscape(s.password.isEmpty() ? storageDefaultPassword() : s.password) + "\",";
  json += "\"ssidMax\":" + String(SSID_MAX_LEN) + ",";
  json += "\"passwordMin\":" + String(PASSWORD_MIN_LEN) + ",";
  json += "\"passwordMax\":" + String(PASSWORD_MAX_LEN) + ",";
  json += "\"rotation\":" + String(displayGetRotation()) + ",";
  json += "\"screen\":" + String(s.screenOn ? 1 : 0) + ",";
  json += "\"led\":" + String(s.ledOn ? 1 : 0) + ",";
  json += "\"screenBright\":" + String(s.screenBright) + ",";
  json += "\"ledBright\":" + String(s.ledBright) + ",";
  json += "\"startDelay\":" + String(s.startDelay) + ",";
  json += "\"startDelayMax\":" + String(START_DELAY_MAX) + ",";
  json += "\"deviceName\":\"" + jsonEscape(s.deviceName.isEmpty()
              ? String(DEVICE_NAME_DEFAULT) : s.deviceName) + "\",";
  json += "\"deviceNameMax\":" + String(DEVICE_NAME_MAX) + ",";
  json += "\"showAccess\":" + String(s.showAccess ? 1 : 0) + ",";
  json += "\"standbyOnBoot\":" + String(s.standbyOnBoot ? 1 : 0) + ",";
  json += "\"unlockSeq\":\"" + jsonEscape(s.unlockSequence) + "\",";
  json += "\"hardlockSeq\":\"" + jsonEscape(s.hardlockSequence) + "\",";
  json += "\"longPressMs\":" + String(s.longPressMs) + ",";
  json += "\"longPressMin\":" + String(LOCK_LONG_PRESS_MIN_MS) + ",";
  json += "\"longPressMax\":" + String(LOCK_LONG_PRESS_MAX_MS) + ",";
  json += "\"lockSeqMax\":" + String(LOCK_SEQ_MAX_LEN) + ",";
  json += "\"hardlockEnabled\":" + String(s.hardlockEnabled ? 1 : 0) + ",";
  json += "\"hardlockReinserts\":" + String(s.hardlockReinserts) + ",";
  json += "\"hardlockReinsertsMin\":" + String(HARDLOCK_REINSERTS_MIN) + ",";
  json += "\"hardlockReinsertsMax\":" + String(HARDLOCK_REINSERTS_MAX) + ",";
  json += "\"screenLocked\":" + String(lockScreenIsLocked() ? 1 : 0) + ",";
  UsbDriveStatus drv = usbDriveGetStatus();
  json += "\"usbDrive\":" + String(drv.exposed ? 1 : 0) + ",";
  json += "\"usbCard\":" + String(drv.cardPresent ? 1 : 0) + ",";
  json += "\"usbSizeMB\":" + String((uint32_t)drv.sizeMB) + ",";
  json += "\"layouts\":[";
  for (size_t i = 0; i < macroLayoutCount(); i++) {
    LayoutInfo li = macroLayoutAt(i);
    if (i) json += ",";
    json += "{\"code\":\"" + String(li.code) + "\",";
    json += "\"name\":\"" + String(li.name) + "\",";
    json += "\"arrangement\":\"" + String(li.arrangement) + "\"}";
  }
  json += "]";
  json += "}";
  sendJson(200, json);
}

static void handleWifiSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  if (!storageSsidIsValid(ssid)) {
    sendError(400, "Network name must be 1 to " + String(SSID_MAX_LEN) + " characters");
    return;
  }
  if (!storagePasswordIsValid(password)) {
    sendError(400, "Password must be " + String(PASSWORD_MIN_LEN) + " to " +
                     String(PASSWORD_MAX_LEN) + " characters");
    return;
  }

  Settings s = storageLoadSettings();
  s.layout = macroGetLayout();
  s.ssid = ssid;
  s.password = password;

  if (!storageSaveSettings(s)) {
    sendError(500, "Could not write to flash");
    return;
  }

  sendJson(200, "{\"ok\":true,\"reboot\":true}");

  displayShowMessage("WI-FI UPDATED", "restarting");
  g_rebootAt = millis() + 1500;
}

static bool parseBright(const String &arg, uint8_t &out) {
  long n = arg.toInt();
  if (n < BRIGHTNESS_MIN || n > BRIGHTNESS_MAX) return false;
  out = (uint8_t)n;
  return true;
}

static void handleDisplaySave() {
  if (lockScreenIsLocked() &&
      (server.hasArg("screen") || server.hasArg("screenBright") ||
       server.hasArg("rotation") || server.hasArg("showAccess"))) {
    sendError(409, "Unlock to change screen settings.");
    return;
  }
  Settings st = storageLoadSettings();
  st.layout = macroGetLayout();

  bool preview = server.hasArg("preview") && server.arg("preview").toInt() != 0;

  if (server.hasArg("screenBright")) {
    if (!parseBright(server.arg("screenBright"), st.screenBright)) {
      sendError(400, "Screen brightness must be " + String(BRIGHTNESS_MIN) +
                     " to " + String(BRIGHTNESS_MAX));
      return;
    }
  }
  if (server.hasArg("ledBright")) {
    if (!parseBright(server.arg("ledBright"), st.ledBright)) {
      sendError(400, "LED brightness must be " + String(BRIGHTNESS_MIN) +
                     " to " + String(BRIGHTNESS_MAX));
      return;
    }
  }

  if (server.hasArg("screen")) st.screenOn = (server.arg("screen").toInt() != 0);
  if (server.hasArg("led")) st.ledOn = (server.arg("led").toInt() != 0);

  if (preview) {
    if (!lockScreenIsLocked()) {
      displaySetScreenOn(st.screenOn);
      displaySetScreenBright(st.screenBright);
    }
    displaySetLedBright(st.ledBright);
    displaySetLed(st.ledOn);
    sendJson(200, "{\"ok\":true}");
    return;
  }

  if (server.hasArg("rotation")) {
    int r = server.arg("rotation").toInt();
    if (r < 0 || r > 3) {
      sendError(400, "Rotation must be 0, 1, 2 or 3");
      return;
    }
    st.rotation = (uint8_t)r;
  }
  if (server.hasArg("showAccess")) st.showAccess = (server.arg("showAccess").toInt() != 0);

  if (!storageSaveSettings(st)) {
    sendError(500, "Could not write to flash");
    return;
  }

  if (!lockScreenIsLocked()) {
    displaySetRotation(st.rotation);
    displaySetScreenOn(st.screenOn);
    displaySetScreenBright(st.screenBright);
  }
  displaySetLedBright(st.ledBright);
  displaySetLed(st.ledOn);

  sendJson(200, "{\"ok\":true}");
}

static void handleDriveSave() {
  if (!server.hasArg("exposed")) {
    sendError(400, "Missing exposed flag");
    return;
  }
  bool exposed = (server.arg("exposed").toInt() != 0);

  Settings st = storageLoadSettings();
  st.layout = macroGetLayout();
  st.usbDrive = exposed;
  if (!storageSaveSettings(st)) {
    sendError(500, "Could not write to flash");
    return;
  }

  usbDriveSetExposed(exposed);

  UsbDriveStatus drv = usbDriveGetStatus();
  if (exposed && !drv.cardPresent) {
    sendError(409, "No card in the slot");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

static bool requireCardOwnership() {
  if (usbDriveFsAvailable()) return true;
  UsbDriveStatus d = usbDriveGetStatus();
  if (!d.cardPresent) {
    sendError(404, "No card in the slot");
  } else {
    sendError(409, "The card is attached to the host. Detach it to browse from here.");
  }
  return false;
}

static void handleSdList() {
  if (!requireCardOwnership()) return;

  String path = server.hasArg("path") ? server.arg("path") : String("/");
  if (path.isEmpty()) path = "/";

  std::vector<SdEntry> entries;
  if (!usbDriveList(path, entries)) {
    sendError(400, "Cannot open " + path);
    return;
  }

  // Up to 256 entries, so the appends below would otherwise realloc and copy
  // the whole listing about a thousand times.
  String json;
  json.reserve(64 + path.length() + entries.size() * 96);

  json = "{\"path\":\"" + jsonEscape(path) + "\",\"entries\":[";
  for (size_t i = 0; i < entries.size(); i++) {
    if (i) json += ",";
    json += "{\"name\":\"" + jsonEscape(entries[i].name) + "\",";
    json += "\"size\":" + String(entries[i].size) + ",";
    json += "\"dir\":" + String(entries[i].isDir ? 1 : 0) + "}";
  }
  json += "]}";
  sendJson(200, json);
}

static void handleSdDownload() {
  if (!requireCardOwnership()) return;

  String path = server.arg("path");
  File f = usbDriveOpen(path);
  if (!f || f.isDirectory()) {
    sendError(404, "File not found");
    return;
  }

  String name = path.substring(path.lastIndexOf('/') + 1);
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

static void handleSdDelete() {
  if (!requireCardOwnership()) return;

  if (!usbDriveDelete(server.arg("path"))) {
    sendError(400, "Could not delete");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

static void handleNameSave() {
  String name = server.arg("name");
  if (!storageDeviceNameIsValid(name)) {
    sendError(400, "Name must be 1 to " + String(DEVICE_NAME_MAX) +
                     " printable characters");
    return;
  }

  Settings st = storageLoadSettings();
  st.layout = macroGetLayout();
  st.deviceName = name;
  if (!storageSaveSettings(st)) {
    sendError(500, "Could not write to flash");
    return;
  }

  usbDriveSetDeviceName(name);
  sendJson(200, "{\"ok\":true}");
}

static void handleSecuritySave() {
  Settings st = storageLoadSettings();
  st.layout = macroGetLayout();
  if (server.hasArg("standbyOnBoot")) {
    st.standbyOnBoot = (server.arg("standbyOnBoot").toInt() != 0);
  }

  if (server.hasArg("unlockSeq")) {
    String v = server.arg("unlockSeq");
    v.toUpperCase();
    if (!storageLockSequenceIsValid(v)) {
      sendError(400, "Unlock sequence must be 1 to " + String(LOCK_SEQ_MAX_LEN) +
                     " characters, each S or L");
      return;
    }
    st.unlockSequence = v;
  }
  if (server.hasArg("hardlockSeq")) {
    String v = server.arg("hardlockSeq");
    v.toUpperCase();
    if (!storageLockSequenceIsValid(v)) {
      sendError(400, "Hard-lock sequence must be 1 to " + String(LOCK_SEQ_MAX_LEN) +
                     " characters, each S or L");
      return;
    }
    st.hardlockSequence = v;
  }
  if (server.hasArg("longPressMs")) {
    long ms = server.arg("longPressMs").toInt();
    if (ms < 0 || ms > 65535 || !storageLongPressIsValid((uint16_t)ms)) {
      sendError(400, "Long-press threshold must be " + String(LOCK_LONG_PRESS_MIN_MS) +
                     " to " + String(LOCK_LONG_PRESS_MAX_MS) + " ms");
      return;
    }
    st.longPressMs = (uint16_t)ms;
  }
  if (server.hasArg("hardlockEnabled")) {
    st.hardlockEnabled = (server.arg("hardlockEnabled").toInt() != 0);
  }
  if (server.hasArg("hardlockReinserts")) {
    long n = server.arg("hardlockReinserts").toInt();
    if (n < HARDLOCK_REINSERTS_MIN || n > HARDLOCK_REINSERTS_MAX) {
      sendError(400, "Reinsertions must be " + String(HARDLOCK_REINSERTS_MIN) +
                     " to " + String(HARDLOCK_REINSERTS_MAX));
      return;
    }
    st.hardlockReinserts = (uint8_t)n;
  }

  if (!storageLockSequencesAreCompatible(st.unlockSequence, st.hardlockSequence)) {
    sendError(400, "Unlock and hard-lock sequences must differ; neither may contain the other");
    return;
  }

  if (!storageSaveSettings(st)) {
    sendError(500, "Could not write to flash");
    return;
  }

  // Takes effect on the next boot into LOCKED, same as the Wi-Fi settings.
  sendJson(200, "{\"ok\":true}");
}

// Locks the screen and the button down to just the unlock gesture, without
// dropping the radio, HID or web server. No-op unless currently unlocked.
static void handleLockDisplay() {
  lockRequestScreenLock();
  sendJson(200, "{\"ok\":true,\"screenLocked\":" + String(lockScreenIsLocked() ? 1 : 0) + "}");
}

// Restores the screen preference, leaving SCREEN_LOCK. Refused while hard-locked.
static void handleUnlockDisplay() {
  bool ok = lockRequestScreenUnlock();
  sendJson(ok ? 200 : 409,
           "{\"ok\":" + String(ok ? "true" : "false") +
           ",\"screenLocked\":" + String(lockScreenIsLocked() ? 1 : 0) + "}");
}

static void handleHardLock() {
  Settings st = storageLoadSettings();
  if (!lockRequestHardLock(st.hardlockReinserts)) {
    sendError(500, "Could not arm hard lock. The device is still online.");
    return;
  }
  sendJson(200, "{\"ok\":true,\"hardlockReinserts\":" + String(st.hardlockReinserts) + "}");
}

static void handleFactoryReset() {
  storageResetSettings();
  sendJson(200, "{\"ok\":true,\"reboot\":true}");
  displayShowMessage("FACTORY RESET", "restarting");
  g_rebootAt = millis() + 1500;
}

static void handleNotFound() {
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

void webBegin(const String &ssid) {
  g_ssid = ssid;

  if (g_captiveDns) dns.start(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/script", HTTP_GET, handleScriptGet);
  server.on("/api/script", HTTP_POST, handleScriptSave);
  server.on("/api/script/delete", HTTP_POST, handleScriptDelete);
  server.on("/api/run", HTTP_POST, handleRun);
  server.on("/api/stop", HTTP_POST, handleStop);
  server.on("/api/tools/start", HTTP_POST, handleToolStart);
  server.on("/api/tools/stop", HTTP_POST, handleToolStop);
  server.on("/api/tools/startup", HTTP_POST, handleToolStartup);
  server.on("/api/tools/setup", HTTP_GET, handleToolSetup);
  server.on("/api/settings/launch", HTTP_POST, handleLaunchOnPlug);
  server.on("/api/layout", HTTP_POST, handleLayout);
  server.on("/api/log", HTTP_GET, handleLog);
  server.on("/api/log/clear", HTTP_POST, handleLogClear);
  server.on("/api/settings", HTTP_GET, handleSettingsGet);
  server.on("/api/settings/wifi", HTTP_POST, handleWifiSave);
  server.on("/api/settings/display", HTTP_POST, handleDisplaySave);
  server.on("/api/settings/drive", HTTP_POST, handleDriveSave);
  server.on("/api/settings/name", HTTP_POST, handleNameSave);
  server.on("/api/settings/security", HTTP_POST, handleSecuritySave);
  server.on("/api/lock-display", HTTP_POST, handleLockDisplay);
  server.on("/api/unlock-display", HTTP_POST, handleUnlockDisplay);
  server.on("/api/hard-lock", HTTP_POST, handleHardLock);
  server.on("/api/sd/list", HTTP_GET, handleSdList);
  server.on("/api/sd/download", HTTP_GET, handleSdDownload);
  server.on("/api/sd/delete", HTTP_POST, handleSdDelete);
  server.on("/api/settings/reset", HTTP_POST, handleFactoryReset);
  server.onNotFound(handleNotFound);

  server.begin();
}

void webLoop() {
  if (g_captiveDns) dns.process();
  server.handleClient();

  if (g_rebootAt && millis() >= g_rebootAt) {
    ESP.restart();
  }
}

void webEnd() {
  g_rebootAt = 0;
  server.stop();
  dns.stop();
}

void webSetCaptiveDns(bool enabled) {
  if (enabled == g_captiveDns) return;
  g_captiveDns = enabled;
  dns.stop();
  if (enabled) dns.start(WiFi.softAPIP());
}
