#pragma once
#include <DNSServer.h>
#include <memory>

// Arduino's AsyncUDP::close() disconnects but retains the bound PCB.
// Rebind to an ephemeral port to release 53. Keep the object alive: AsyncUDP
// has queued callbacks referencing it, which cannot be safely destroyed here.
class CaptiveDns {
public:
  void start(IPAddress ip) {
    if (!server) server.reset(new DNSServer());
    address = ip;
    server->setErrorReplyCode(DNSReplyCode::NoError);
    server->setTTL(0);
    server->start(53, "*", ip);
    running = true;
  }
  void stop() {
    if (server && running) {
      server->start(0, "*", address);
      server->stop();
    }
    running = false;
  }
  void process() { if (running) server->processNextRequest(); }
private:
  std::unique_ptr<DNSServer> server;
  IPAddress address;
  bool running = false;
};
