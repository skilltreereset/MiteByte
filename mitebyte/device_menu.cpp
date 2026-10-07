#include "device_menu.h"
#include "config.h"
#include "lock.h"
#include "macro.h"
#include "storage.h"
#include "tools.h"
#include "ui_display.h"
#include <vector>

// What each entry does when chosen. BACK is always first; scripts hold their
// file name, tools their ID.
enum MenuKind : uint8_t { MENU_BACK, MENU_SCRIPT, MENU_TOOL };
struct Entry {
  MenuKind kind;
  String target;
};

static bool s_open = false;
static std::vector<Entry> s_entries;
// Static, and read by reference by the menu view for as long as it is on screen.
static std::vector<MenuItem> s_items;
static size_t s_selected = 0;
static uint32_t s_idleSince = 0;

static void build() {
  s_entries.clear();
  s_items.clear();
  s_entries.push_back({MENU_BACK, ""});
  s_items.push_back({"BACK", false});

  for (const ScriptInfo &info : storageListDetailed()) {
    String label = info.name;
    if (label.endsWith(".txt")) label.remove(label.length() - 4);
    s_entries.push_back({MENU_SCRIPT, info.name});
    s_items.push_back({label, false});
  }

  const String active = toolsActiveId();
  for (size_t i = 0; i < toolsCount(); i++) {
    const ToolPlugin *tool = toolsAt(i);
    s_entries.push_back({MENU_TOOL, tool->id});
    s_items.push_back({tool->title, active == tool->id});
  }
}

static void paint(const char *alert = nullptr) {
  displayWake();  // keeps a screen that is set to off lit while browsing
  displayShowMenu(s_items, s_selected, alert);
}

static void runScript(const String &name) {
  if (toolsRunning()) { paint("STOP TOOL"); return; }
  MacroStatus st = macroGetStatus();
  if (st.state == MACRO_RUNNING || st.state == MACRO_ARMED) { paint("BUSY"); return; }

  String script = storageRead(name);
  if (script.isEmpty()) { paint("EMPTY"); return; }

  // Same start delay as a run from the web UI.
  if (!macroRun(script, name, storageLoadSettings().startDelay)) { paint("BUSY"); return; }
  menuClose();
}

// A tool toggles: the menu stays open showing its new state, so the same
// hold switches it back. A tool that takes over the display closes the menu
// instead, so it is on screen at once.
static void toggleTool(const String &id) {
  if (toolsActiveId() == id) {
    toolsStop();
  } else if (toolsRunning()) {
    paint("STOP TOOL");
    return;
  } else {
    String error;
    if (!toolsStart(id, error)) { paint("FAILED"); return; }
    const ToolPlugin *tool = toolsFind(id);
    if (tool && tool->takesScreen) { menuClose(); return; }
  }
  build();
  paint();
}

// Where the menu opens. Whatever is running is what someone holding the button
// most likely wants (to stop the tool), so it starts there; with more than one
// running, the first by name. Otherwise it starts on the first real entry
// rather than BACK.
static size_t startIndex(const String &tool) {
  const MacroStatus script = macroGetStatus();
  const bool scriptBusy = script.state == MACRO_RUNNING || script.state == MACRO_ARMED;
  const String origin = scriptBusy ? macroRunOrigin() : String();

  size_t best = 0;  // 0 is BACK, never a running thing
  String bestName;
  for (size_t i = 1; i < s_entries.size(); i++) {
    const Entry &entry = s_entries[i];
    const bool running = entry.kind == MENU_TOOL ? !tool.isEmpty() && entry.target == tool
                                                 : scriptBusy && entry.target == origin;
    if (!running) continue;
    String name = s_items[i].label;
    name.toLowerCase();
    if (best == 0 || name < bestName) {
      best = i;
      bestName = name;
    }
  }
  if (best) return best;
  return s_items.size() > 1 ? 1 : 0;
}

bool menuIsOpen() { return s_open; }

void menuOpen() {
  // A tool that takes over the display cannot share it with the menu, so
  // opening the menu switches it off. The menu still opens on it, and one more
  // hold turns it back on.
  const String tool = toolsActiveId();
  const ToolPlugin *active = toolsFind(tool);
  if (active && active->takesScreen) toolsStop();

  build();
  s_open = true;
  s_selected = startIndex(tool);
  s_idleSince = millis();
  paint();
}

void menuClose() {
  s_open = false;
  s_entries.clear();
  s_items.clear();
}

void menuPress(bool hold) {
  if (!s_open) return;
  s_idleSince = millis();

  if (!hold) {
    s_selected = (s_selected + 1) % s_items.size();
    paint();
    return;
  }

  const Entry entry = s_entries[s_selected];
  switch (entry.kind) {
    case MENU_BACK:   menuClose(); break;
    case MENU_SCRIPT: runScript(entry.target); break;
    case MENU_TOOL:   toggleTool(entry.target); break;
  }
}

bool menuTick() {
  if (!s_open) return false;
  displayMenuHold(lockHeldMs());
  if (millis() - s_idleSince < MENU_IDLE_MS) return false;
  menuClose();
  return true;
}
