#include "hotspot_dns.h"
#include "dns_packet.h"
#include <WiFi.h>
#include <lwip/sockets.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <cstdio>
#include <atomic>

// Keep the AP's DHCP DNS address. Bounded, nonblocking forwarding supports
// eight UDP queries and two TCP clients, including large DNS/TCP replies.
static int s_clients = -1, s_resolver = -1, s_listener = -1;
static char s_error[96] = {};
const char *hotspotDnsError() { return s_error; }
static uint16_t s_nextId = 0;
static std::atomic<uint32_t> s_upstream{0}, s_requests{0}, s_forwarded{0}, s_replies{0}, s_timeouts{0}, s_rejected{0};
static std::atomic<int> s_socketError{0};
HotspotDnsStatus hotspotDnsStatus() {
  return {s_upstream.load(), s_requests.load(), s_forwarded.load(), s_replies.load(),
          s_timeouts.load(), s_rejected.load(), s_socketError.load()};
}
// Pending UDP query lifetime, and TCP connect/relay idle timeout.
static constexpr uint32_t kUdpQueryMs = 3000;
static constexpr uint32_t kTcpIdleMs = 10000;
// Single shared scratch: all access is from receiveWorker under s_ioLock only.
static uint8_t s_packet[4097]; // one sentinel byte detects oversized datagrams
struct Query {
  bool used;
  sockaddr_in client;
  uint32_t resolver;
  uint16_t id, original;
  uint32_t deadline;
};
static Query s_queries[8];
struct Pipe {
  uint8_t bytes[512];
  size_t size, sent;
  bool eof, shutdown;
};
struct TcpQuery {
  int client = -1, resolver = -1;
  bool connecting = false;
  uint32_t deadline = 0;
  Pipe request = {}, response = {};
};
static TcpQuery s_tcp[2];
// s_packet accessors: receiveWorker/s_ioLock only, never from another task.
static uint16_t getId() { return (uint16_t(s_packet[0]) << 8) | s_packet[1]; }
static void setId(uint16_t id) { s_packet[0] = id >> 8; s_packet[1] = id & 0xff; }
static sockaddr_in address(uint32_t ip, uint16_t port) {
  sockaddr_in value = {};
  value.sin_family = AF_INET; value.sin_addr.s_addr = ip; value.sin_port = htons(port);
  return value;
}
static void closeSocket(int &fd) { if (fd >= 0) close(fd); fd = -1; }
static int openSocket(int type) {
  int fd = socket(AF_INET, type, 0);
  if (fd >= 0 && fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
    int saved = errno; closeSocket(fd); errno = saved;
  }
  if (fd >= 0 && type == SOCK_STREAM) {
    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
      int saved = errno; closeSocket(fd); errno = saved;
    }
  }
  return fd;
}
static bool bindSocket(int fd, uint32_t ip, uint16_t port) {
  auto endpoint = address(ip, port);
  return fd >= 0 && bind(fd, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)) == 0;
}
static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
static void closeTcp(TcpQuery &q) {
  closeSocket(q.client); closeSocket(q.resolver);
  q = TcpQuery{};
}
bool hotspotDnsBegin() {
  s_error[0] = 0;
  if (s_clients >= 0) return true;
  s_upstream = 0; s_requests = 0; s_forwarded = 0; s_replies = 0;
  s_timeouts = 0; s_rejected = 0; s_socketError = 0;
  for (auto &q : s_queries) q.used = false;
  uint32_t ap = uint32_t(WiFi.softAPIP());
  auto fail = [](const char *stage) {
    snprintf(s_error, sizeof(s_error), "Hotspot DNS: %s failed (error %d)", stage, errno);
    hotspotDnsStop(); return false;
  };
  s_clients = openSocket(SOCK_DGRAM);
  if (s_clients < 0) return fail("UDP socket");
  if (!bindSocket(s_clients, ap, 53)) return fail("UDP bind");
  s_resolver = openSocket(SOCK_DGRAM);
  if (s_resolver < 0) return fail("resolver socket");
  if (!bindSocket(s_resolver, INADDR_ANY, 0)) return fail("resolver bind");
  s_listener = openSocket(SOCK_STREAM);
  if (s_listener < 0) return fail("TCP socket");
  if (!bindSocket(s_listener, ap, 53)) return fail("TCP bind");
  if (listen(s_listener, 2) != 0) return fail("TCP listen");
  return true;
}
bool hotspotDnsBusy() {
  if (s_clients < 0) return false;
  for (const auto &q : s_queries) if (q.used) return true;
  for (const auto &q : s_tcp) if (q.client >= 0) return true;
  return false;
}
void hotspotDnsStop() {
  closeSocket(s_clients); closeSocket(s_resolver); closeSocket(s_listener);
  for (auto &q : s_queries) q.used = false;
  for (auto &q : s_tcp) closeTcp(q);
}
// Never forward a silently truncated DNS datagram.
static int receive(int fd, sockaddr_in &source) {
  socklen_t length = sizeof(source);
  int read = recvfrom(fd, s_packet, sizeof(s_packet), 0,
                      reinterpret_cast<sockaddr *>(&source), &length);
  if (read < 0 && !wouldBlock()) s_socketError = errno;
  return read < 0 ? 0 : read == int(sizeof(s_packet)) ? -1 : read;
}
static bool sendPacket(int fd, const sockaddr_in &to, int size) {
  int result = sendto(fd, s_packet, size, 0, reinterpret_cast<const sockaddr *>(&to), sizeof(to));
  if (result != size) s_socketError = result < 0 ? errno : EMSGSIZE;
  return result == size;
}
static bool relay(int from, int to, Pipe &pipe, uint32_t &deadline) {
  if (!pipe.size && !pipe.eof) {
    int count = recv(from, pipe.bytes, sizeof(pipe.bytes), 0);
    if (count > 0) { pipe.size = count; pipe.sent = 0; deadline = millis() + kTcpIdleMs; }
    else if (!count) pipe.eof = true;
    else if (!wouldBlock()) return false;
  }
  if (pipe.size) {
    int count = send(to, pipe.bytes + pipe.sent, pipe.size - pipe.sent, 0);
    if (count > 0) {
      pipe.sent += count; deadline = millis() + kTcpIdleMs;
      if (pipe.sent == pipe.size) pipe.size = pipe.sent = 0;
    } else if (count < 0 && !wouldBlock()) return false;
  }
  if (pipe.eof && !pipe.size && !pipe.shutdown) {
    shutdown(to, SHUT_WR); pipe.shutdown = true;
  }
  return true;
}
static void tcpTick(uint32_t resolver, uint32_t now) {
  for (auto &q : s_tcp) {
    if (q.client < 0) continue;
    if (!resolver || int32_t(now - q.deadline) >= 0) { closeTcp(q); continue; }
    if (q.connecting) {
      fd_set writable; FD_ZERO(&writable); FD_SET(q.resolver, &writable);
      timeval timeout = {};
      int selected = select(q.resolver + 1, nullptr, &writable, nullptr, &timeout);
      if (selected < 0) { closeTcp(q); continue; }
      if (!selected) continue;
      int error = 0; socklen_t size = sizeof(error);
      if (getsockopt(q.resolver, SOL_SOCKET, SO_ERROR, &error, &size) < 0 || error) {
        closeTcp(q); continue;
      }
      q.connecting = false;
    }
    if (!relay(q.client, q.resolver, q.request, q.deadline) ||
        !relay(q.resolver, q.client, q.response, q.deadline) ||
        (q.response.eof && !q.response.size)) closeTcp(q);
  }
  // One accept per tick prevents connection attempts from monopolising UI time.
  int client = accept(s_listener, nullptr, nullptr);
  if (client < 0) return;
  TcpQuery *slot = nullptr;
  for (auto &q : s_tcp) if (q.client < 0) { slot = &q; break; }
  if (!slot || !resolver || fcntl(client, F_SETFL, O_NONBLOCK) < 0) { close(client); return; }
  int upstream = openSocket(SOCK_STREAM);
  if (upstream < 0) { close(client); return; }
  auto endpoint = address(resolver, 53);
  int result = connect(upstream, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint));
  if (result < 0 && errno != EINPROGRESS) { close(client); close(upstream); return; }
  slot->client = client; slot->resolver = upstream;
  slot->connecting = result < 0; slot->deadline = now + kTcpIdleMs;
}
void hotspotDnsTick(IPAddress resolver) {
  if (s_clients < 0) return;
  s_upstream = uint32_t(resolver);
  uint32_t now = millis();
  for (auto &q : s_queries) if (q.used && int32_t(now - q.deadline) >= 0) { q.used = false; ++s_timeouts; }
  for (int budget = 0; budget < 4; ++budget) {
    sockaddr_in source = {};
    int size = receive(s_resolver, source);
    if (!size) break;
    if (size < 12 || !(s_packet[2] & 0x80) || source.sin_port != htons(53)) continue;
    for (auto &q : s_queries) {
      if (!q.used || q.id != getId() || q.resolver != source.sin_addr.s_addr) continue;
      setId(q.original); if (sendPacket(s_clients, q.client, size)) ++s_replies; q.used = false;
      break;
    }
  }
  for (int budget = 0; budget < 4; ++budget) {
    sockaddr_in client = {};
    int size = receive(s_clients, client);
    if (!size) break;
    ++s_requests;
    if (size < 12 || !hotspotDnsPrepare(s_packet, size)) { ++s_rejected; continue; }
    Query *slot = nullptr;
    for (auto &q : s_queries) if (!q.used) { slot = &q; break; }
    if (!slot || !uint32_t(resolver)) {
      ++s_rejected;
      s_packet[2] = 0x80 | (s_packet[2] & 1);
      s_packet[3] = 0x82;
      s_packet[6] = s_packet[7] = s_packet[8] = s_packet[9] = 0;
      sendPacket(s_clients, client, size); continue;
    }
    uint16_t original = getId();
    bool occupied;
    do {
      ++s_nextId; occupied = false;
      for (const auto &q : s_queries) if (q.used && q.id == s_nextId) occupied = true;
    } while (occupied);
    *slot = {true, client, uint32_t(resolver), s_nextId, original, now + kUdpQueryMs};
    setId(slot->id);
    auto endpoint = address(uint32_t(resolver), 53);
    if (sendPacket(s_resolver, endpoint, size)) ++s_forwarded;
    else slot->used = false;
  }
  tcpTick(uint32_t(resolver), now);
}
