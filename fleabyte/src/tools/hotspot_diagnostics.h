#pragma once
#include <stdint.h>
#include <stddef.h>

// Master switch for the whole SD diagnostic subsystem -- summaries, lifecycle
// and error logs, plus the writer task, record queue and SD writes. OFF by
// default so a normal build ships with no logging and ~9 KB more free heap and
// zero logging overhead. Build with -DHOTSPOT_DIAGNOSTICS=1 to enable logging
// (needed for the RAM A/B). When off, every hotspotDiagnostic* call is a no-op.
#ifndef HOTSPOT_DIAGNOSTICS
#define HOTSPOT_DIAGNOSTICS 0
#endif

// Verbose (per-packet and endpoint-register) detail within the diagnostics:
// per-packet USB_RX/USB_TX, USB_CONTROL, USB_HW. OFF by default; it floods the
// log and adds CPU/SD overhead under load. Add -DHOTSPOT_VERBOSE=1 (with
// diagnostics enabled) for the full trace. The 2 s STATE/IO/FLOW/MEM/DNS
// summaries and error/lifecycle lines are the always-on baseline when
// diagnostics are enabled. See the RAM backlog in usb_hotspot.cpp (E1/E2).
#ifndef HOTSPOT_VERBOSE
#define HOTSPOT_VERBOSE 0
#endif
#if HOTSPOT_VERBOSE && HOTSPOT_DIAGNOSTICS
#define HOTSPOT_VLOG(...) hotspotDiagnosticLog(__VA_ARGS__)
#define HOTSPOT_VPACKET(dir, data, size) hotspotDiagnosticPacket((dir), (data), (size))
#else
#define HOTSPOT_VLOG(...) ((void)0)
#define HOTSPOT_VPACKET(dir, data, size) ((void)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif
void hotspotDiagnosticLog(const char *format, ...);
void hotspotDiagnosticPacket(const char *direction, const uint8_t *data, size_t size);
#ifdef __cplusplus
}
bool hotspotDiagnosticsBegin();
void hotspotDiagnosticsEnd();
const char *hotspotDiagnosticsState();
unsigned hotspotDiagnosticsDropped();
#endif
