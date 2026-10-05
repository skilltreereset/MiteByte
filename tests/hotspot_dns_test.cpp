#include <cassert>
#include <cstdio>
#include <deque>
#include <map>
#include <vector>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include "../fleabyte/captive_dns.h"
#include "../fleabyte/src/tools/hotspot_dns.cpp"

struct Datagram { sockaddr_in peer; std::vector<uint8_t> bytes; };
struct Socket {
  int type = 0;
  sockaddr_in peer = {};
  std::deque<Datagram> input, output;
  std::vector<uint8_t> incoming, outgoing;
  bool eof = false, blocked = false, shut = false;
  size_t sendLimit = 17;
};
static std::map<int, Socket> sockets;
static std::map<int, Socket> closed;
static std::deque<int> accepts;
static int nextFd = 1, connectError = 0;
static bool connectReady = true;
static uint32_t now = 0;
uint32_t millis() { return now; }
int fakeSocket(int, int type, int) { int fd = nextFd++; sockets[fd].type = type; return fd; }
int fakeClose(int fd) { assert(sockets.count(fd)); closed[fd] = sockets.at(fd); sockets.erase(fd); return 0; }
int fakeFcntl(int, int, int) { return 0; }
int fakeBind(int fd, const sockaddr *a, socklen_t) {
  auto endpoint = *reinterpret_cast<const sockaddr_in *>(a);
  if (sockets[fd].type == SOCK_DGRAM && endpoint.sin_port == htons(53) && captivePortReserved) {
    errno = EADDRINUSE; return -1;
  }
  sockets[fd].peer = endpoint; return 0;
}
int fakeListen(int, int) { return 0; }
int fakeAccept(int, sockaddr *, socklen_t *) {
  if (accepts.empty()) { errno = EAGAIN; return -1; }
  int fd = accepts.front(); accepts.pop_front(); return fd;
}
int fakeConnect(int fd, const sockaddr *a, socklen_t) {
  sockets[fd].peer = *reinterpret_cast<const sockaddr_in *>(a); errno = EINPROGRESS; return -1;
}
int fakeSelect(int, fd_set *, fd_set *, fd_set *, timeval *timeout) {
  assert(timeout && !timeout->tv_sec && !timeout->tv_usec); // loop must never block
  return connectReady ? 1 : 0;
}
int fakeGetSockOpt(int, int, int, void *p, socklen_t *) { *static_cast<int *>(p) = connectError; return 0; }
int fakeSetSockOpt(int, int, int, const void *, socklen_t) { return 0; }
int fakeShutdown(int fd, int) { sockets[fd].shut = true; return 0; }
int fakeRecv(int fd, void *p, size_t size, int) {
  auto &s = sockets[fd];
  if (s.incoming.empty()) { if (s.eof) return 0; errno = EAGAIN; return -1; }
  size = std::min(size, s.incoming.size()); memcpy(p, s.incoming.data(), size);
  s.incoming.erase(s.incoming.begin(), s.incoming.begin() + size); return size;
}
int fakeSend(int fd, const void *p, size_t size, int) {
  auto &s = sockets[fd];
  if (s.blocked) { errno = EAGAIN; return -1; }
  size = std::min(size, s.sendLimit);
  auto bytes = static_cast<const uint8_t *>(p); s.outgoing.insert(s.outgoing.end(), bytes, bytes + size);
  return size;
}
int fakeRecvFrom(int fd, void *p, size_t size, int, sockaddr *a, socklen_t *) {
  auto &s = sockets[fd];
  if (s.input.empty()) { errno = EAGAIN; return -1; }
  auto packet = s.input.front(); s.input.pop_front();
  *reinterpret_cast<sockaddr_in *>(a) = packet.peer;
  size = std::min(size, packet.bytes.size()); memcpy(p, packet.bytes.data(), size); return size;
}
int fakeSendTo(int fd, const void *p, size_t size, int, const sockaddr *a, socklen_t) {
  auto bytes = static_cast<const uint8_t *>(p);
  sockets[fd].output.push_back({*reinterpret_cast<const sockaddr_in *>(a), {bytes, bytes + size}});
  return size;
}
static int client() { int fd = fakeSocket(AF_INET, SOCK_STREAM, 0); accepts.push_back(fd); return fd; }
int main() {
  {
    DNSServer legacy;
    legacy.start(53, "*", WiFi.softAPIP()); legacy.stop();
    assert(!hotspotDnsBegin()); // reproduces the old portal-to-hotspot failure
    assert(sockets.empty());
    assert(strstr(hotspotDnsError(), "UDP bind"));
    assert(strstr(hotspotDnsError(), std::to_string(EADDRINUSE).c_str()));
  }
  CaptiveDns captive;
  for (int i = 0; i < 3; ++i) {
    captive.start(WiFi.softAPIP()); assert(captivePortReserved);
    captive.stop(); assert(!captivePortReserved);
    assert(hotspotDnsBegin()); assert(!*hotspotDnsError());
    hotspotDnsStop(); assert(sockets.empty());
  }
  const IPAddress resolver(0x0101a8c0);
  const auto remote = address(uint32_t(resolver), 53), phone = address(0x0204a8c0, 50000);
  assert(hotspotDnsBegin()); assert(sockets.size() == 3);
  assert(sockets[s_clients].peer.sin_addr.s_addr == uint32_t(WiFi.softAPIP()));
  std::vector<uint8_t> query = {0x12,0x34,1,0,0,1,0,0,0,0,0,0,1,'a',0,0,1,0,1};
  sockets[s_clients].input.push_back({phone, query}); hotspotDnsTick(resolver);
  auto forwarded = sockets[s_resolver].output.front().bytes;
  assert(hotspotDnsStatus().resolver == uint32_t(resolver));
  assert(hotspotDnsStatus().requests == 1 && hotspotDnsStatus().forwarded == 1 && !hotspotDnsStatus().replies);
  assert(forwarded != query); // translated transaction ID
  forwarded[2] |= 0x80;
  auto impostor = remote; impostor.sin_addr.s_addr++;
  sockets[s_resolver].input.push_back({impostor, forwarded}); hotspotDnsTick(resolver);
  assert(sockets[s_clients].output.empty());
  assert(!hotspotDnsStatus().replies);
  sockets[s_resolver].input.push_back({remote, forwarded}); hotspotDnsTick(resolver);
  auto answer = sockets[s_clients].output.back().bytes;
  assert(answer[0] == 0x12 && answer[1] == 0x34 && (answer[2] & 0x80));
  assert(hotspotDnsStatus().replies == 1);
  sockets[s_clients].input.push_back({phone, std::vector<uint8_t>(5000, 0)});
  size_t count = sockets[s_resolver].output.size(); hotspotDnsTick(resolver);
  assert(sockets[s_resolver].output.size() == count); // oversized datagram discarded
  sockets[s_clients].input.push_back({phone, query}); hotspotDnsTick(IPAddress());
  assert((sockets[s_clients].output.back().bytes[3] & 15) == 2); // no upstream: SERVFAIL
  for (int i = 0; i < 9; ++i) sockets[s_clients].input.push_back({phone, query});
  for (int i = 0; i < 3; ++i) hotspotDnsTick(resolver);
  assert((sockets[s_clients].output.back().bytes[3] & 15) == 2); // bounded capacity
  now += 3001; hotspotDnsTick(resolver);
  for (const auto &q : s_queries) assert(!q.used);
  assert(hotspotDnsStatus().timeouts == 8 && hotspotDnsStatus().rejected == 3);

  int downstream = client(); hotspotDnsTick(resolver);
  int upstream = s_tcp[0].resolver; assert(upstream >= 0);
  std::vector<uint8_t> large(4098); // DNS length prefix plus a large answer
  for (size_t i = 0; i < large.size(); ++i) large[i] = i & 255;
  sockets[downstream].incoming = query; sockets[upstream].incoming = large;
  sockets[upstream].eof = true; sockets[downstream].blocked = true;
  hotspotDnsTick(resolver);
  assert(sockets[downstream].outgoing.empty() && s_tcp[0].response.size == 512);
  sockets[downstream].blocked = false;
  // Partial writes preserve both directions and flush the reply before EOF.
  for (int i = 0; i < 300 && s_tcp[0].client >= 0; ++i) hotspotDnsTick(resolver);
  assert(s_tcp[0].client < 0);
  assert(closed.at(downstream).outgoing == large);
  assert(closed.at(upstream).outgoing == query);
  assert(sockets.size() == 3);

  connectReady = false;
  client(); hotspotDnsTick(resolver);
  now += 10001; hotspotDnsTick(resolver); assert(sockets.size() == 3);
  connectReady = true; connectError = ECONNREFUSED;
  client(); hotspotDnsTick(resolver); hotspotDnsTick(resolver); assert(sockets.size() == 3);
  connectError = 0;
  client(); hotspotDnsTick(resolver); client(); hotspotDnsTick(resolver);
  int third = client(); hotspotDnsTick(resolver); assert(!sockets.count(third));
  hotspotDnsStop(); assert(sockets.empty());
  assert(hotspotDnsBegin()); hotspotDnsStop(); assert(sockets.empty());
  puts("PASS: captive DNS port handoff, UDP identity/capacity/size, TCP backpressure, timeout, failure and cleanup");
}
