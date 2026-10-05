#pragma once
#include <IPAddress.h>
struct HotspotDnsStatus {
  uint32_t resolver, requests, forwarded, replies, timeouts, rejected;
  int socketError;
};
HotspotDnsStatus hotspotDnsStatus();
bool hotspotDnsBegin();
const char *hotspotDnsError();
void hotspotDnsStop();
void hotspotDnsTick(IPAddress resolver);
// True while a UDP query is awaiting a reply or a TCP relay is open, i.e. the
// proxy needs frequent servicing. False lets the receive worker idle longer.
bool hotspotDnsBusy();
