#pragma once
#include <Arduino.h>

enum MacroState : uint8_t {
  MACRO_IDLE,
  MACRO_ARMED,
  MACRO_RUNNING,
  MACRO_DONE,
  MACRO_ABORTED,
  MACRO_ERROR,
};

struct MacroStatus {
  MacroState state;
  int line;
  int total;
  int countdown;
  String message;

  // Counts finished runs. Callers that sample the status periodically cannot
  // spot a run that started and ended between two samples by comparing
  // states, which is how a quick second run went unreported.
  uint32_t finishSeq;
  bool finishFailed;
};

void macroBegin();

bool macroRun(const String &script, const String &origin, uint16_t delaySeconds);

void macroAbort();

MacroStatus macroGetStatus();
bool macroIsRunning();

// True once the host has sent a keyboard LED report, which it does on
// enumeration and whenever a lock key changes. It is the only signal the
// device gets that the other end has finished enumerating and is
// listening, rather than a guess dressed up as a delay.
bool macroHostSeen();

bool macroSetLayout(const String &code);
String macroGetLayout();

String macroGetArrangement();

struct LayoutInfo {
  const char *code;
  const char *name;
  const char *arrangement;
};
size_t macroLayoutCount();
LayoutInfo macroLayoutAt(size_t i);
bool macroLayoutExists(const String &code);

void macroLog(const String &line);

// The log is polled separately from the status so a four kilobyte string
// is not serialised into every status response. seq counts bytes ever
// appended: a client passes back what it has and receives only the rest.
String macroGetLogSince(uint32_t since, uint32_t &seqOut);
uint32_t macroClearLog();
