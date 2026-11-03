#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace minislot {

struct Endpoint {
  std::string host;
  uint16_t port = 0;
};

// Thin RAII wrapper over a POSIX UDP socket. DOCSIS MAC management
// messages ride on an unreliable broadcast-style medium (the shared
// upstream/downstream channel), which UDP models far more honestly than
// TCP would - a CM that misses a MAP just waits for the next one, exactly
// like a dropped UDP datagram. Non-blocking receive with select()-based
// timeout lets CM and CMTS loops stay responsive to shutdown signals.
class UdpSocket {
 public:
  explicit UdpSocket(uint16_t bindPort /* 0 = ephemeral */);
  ~UdpSocket();

  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

  void enableBroadcast();
  void sendTo(const Endpoint& dest, const std::vector<uint8_t>& payload) const;

  // Waits up to timeoutMs for a datagram. Returns the payload and sender
  // endpoint, or std::nullopt on timeout.
  std::optional<std::pair<std::vector<uint8_t>, Endpoint>> receive(int timeoutMs) const;

  uint16_t localPort() const { return boundPort_; }

 private:
  int fd_ = -1;
  uint16_t boundPort_ = 0;
};

class SocketError : public std::runtime_error {
 public:
  explicit SocketError(const std::string& what) : std::runtime_error(what) {}
};

}  // namespace minislot
