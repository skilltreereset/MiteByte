#pragma once
#include <Arduino.h>
#include <vector>

struct ToolDetail { String label; String value; };
struct ToolStatus {
  bool running;
  String state;
  String message;
  std::vector<ToolDetail> details;
};

// Compile-time plugins: one registration, a lifecycle and a status snapshot.
// Plugins own their implementation and settings. No script interpreter hooks
// or remote command execution are needed for a long-running device mode.
struct ToolPlugin {
  const char *id;
  const char *title;
  const char *description;
  void (*begin)();
  bool (*start)(String &error);
  void (*stop)();
  void (*tick)();
  ToolStatus (*status)();
  const char *notice;
  const char *setupName;
  const char *setupContent;
  const char *setupTitle;
  const char *setupHint;
  const char *setupManual;
  const char *setupScriptName = nullptr;
};

void toolsBegin();
size_t toolsCount();
const ToolPlugin *toolsAt(size_t index);
const ToolPlugin *toolsFind(const String &id);
ToolStatus toolsStatus(const ToolPlugin &plugin);
bool toolsStart(const String &id, String &error);
void toolsStop();
bool toolsRunning();
String toolsActiveId();
String toolsStartupId();
bool toolsSetStartup(const String &id);
void toolsOnline();
void toolsOffline();
void toolsTick(bool scriptBusy);
void toolsReset();
