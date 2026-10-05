#pragma once
#include <IPAddress.h>
#include <cassert>

enum class DNSReplyCode { NoError };
inline bool captivePortReserved = false;
class DNSServer {
public:
  ~DNSServer() { captivePortReserved = false; }
  void setErrorReplyCode(DNSReplyCode) {}
  void setTTL(int ttl) { assert(ttl == 0); }
  bool start(int port, const char *, IPAddress) {
    assert(port == 53 || port == 0);
    captivePortReserved = port == 53;
    return true;
  }
  // Model Arduino 3.3.12: close disconnects, but the PCB stays bound.
  void stop() {}
  void processNextRequest() {}
};
