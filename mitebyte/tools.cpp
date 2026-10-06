#include "tools.h"
#include "src/tools/hotspot/usb_hotspot.h"
#include "storage.h"

// Register future tools here. HTTP, the library and startup behavior use this
// same table and do not need tool-specific branches.
static const ToolPlugin *const s_plugins[] = { &USB_HOTSPOT_TOOL };
static const ToolPlugin *s_active = nullptr;
static String s_startup;
static String s_failedId, s_failure;
static bool s_online = false, s_pendingStartup = false;

size_t toolsCount() { return sizeof(s_plugins) / sizeof(s_plugins[0]); }
const ToolPlugin *toolsAt(size_t index) {
  return index < toolsCount() ? s_plugins[index] : nullptr;
}
const ToolPlugin *toolsFind(const String &id) {
  for (const ToolPlugin *p : s_plugins) if (id == p->id) return p;
  return nullptr;
}
ToolStatus toolsStatus(const ToolPlugin &plugin) {
  ToolStatus status = plugin.status();
  if (!status.running && s_failedId == plugin.id) {
    status.state = "error"; status.message = s_failure;
  }
  return status;
}
void toolsBegin() {
  s_startup = storageToolStartupRead();
  if (!toolsFind(s_startup)) s_startup = "";
  for (const ToolPlugin *p : s_plugins) p->begin();
}
bool toolsStart(const String &id, String &error) {
  const ToolPlugin *plugin = toolsFind(id);
  if (!plugin) { error = "Tool not found"; return false; }
  if (!s_online) { error = "Unlock the device first"; return false; }
  if (s_active == plugin) return true;
  if (s_active) { error = "Stop the current tool first"; return false; }
  s_pendingStartup = false;
  if (!plugin->start(error)) {
    s_failedId = id; s_failure = error; return false;
  }
  s_failedId = ""; s_failure = "";
  s_active = plugin;
  return true;
}
void toolsStop() {
  s_failedId = ""; s_failure = "";
  s_pendingStartup = false;
  if (s_active) { s_active->stop(); s_active = nullptr; }
}
bool toolsRunning() { return s_active != nullptr; }
String toolsActiveId() { return s_active ? String(s_active->id) : String(); }
String toolsStartupId() { return s_startup; }
bool toolsSetStartup(const String &id) {
  if (!id.isEmpty() && !toolsFind(id)) return false;
  bool ok = storageToolStartupWrite(id);
  if (ok) s_startup = id;
  return ok;
}
void toolsOnline() { s_online = true; s_pendingStartup = !s_startup.isEmpty(); }
void toolsOffline() { toolsStop(); s_online = false; }
void toolsTick(bool scriptBusy) {
  if (s_online && s_pendingStartup && !scriptBusy) {
    String error;
    s_pendingStartup = false;
    toolsStart(s_startup, error);
  }
  if (s_active) s_active->tick();
}
void toolsReset() { toolsOffline(); toolsSetStartup(""); }
