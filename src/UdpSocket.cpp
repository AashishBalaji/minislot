#include "minislot/UdpSocket.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>

#include <cstring>

namespace minislot {

UdpSocket::UdpSocket(uint16_t bindPort) {
  fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ < 0) throw SocketError("socket() failed: " + std::string(strerror(errno)));

  int reuse = 1;
  ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(bindPort);

  if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd_);
    throw SocketError("bind() failed: " + std::string(strerror(errno)));
  }

  socklen_t len = sizeof(addr);
  if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
    boundPort_ = ntohs(addr.sin_port);
  }
}

UdpSocket::~UdpSocket() {
  if (fd_ >= 0) ::close(fd_);
}

void UdpSocket::enableBroadcast() {
  int yes = 1;
  if (::setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) < 0) {
    throw SocketError("setsockopt(SO_BROADCAST) failed: " + std::string(strerror(errno)));
  }
}

void UdpSocket::sendTo(const Endpoint& dest, const std::vector<uint8_t>& payload) const {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(dest.port);
  if (::inet_pton(AF_INET, dest.host.c_str(), &addr.sin_addr) != 1) {
    throw SocketError("invalid destination address: " + dest.host);
  }

  ssize_t sent = ::sendto(fd_, payload.data(), payload.size(), 0,
                           reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (sent < 0) throw SocketError("sendto() failed: " + std::string(strerror(errno)));
}

std::optional<std::pair<std::vector<uint8_t>, Endpoint>> UdpSocket::receive(int timeoutMs) const {
  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(fd_, &readSet);

  timeval tv{};
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;

  int ready = ::select(fd_ + 1, &readSet, nullptr, nullptr, &tv);
  if (ready <= 0) return std::nullopt;  // timeout or error - caller treats both as "nothing yet"

  uint8_t buf[2048];
  sockaddr_in from{};
  socklen_t fromLen = sizeof(from);
  ssize_t n = ::recvfrom(fd_, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
  if (n < 0) return std::nullopt;

  char hostBuf[INET_ADDRSTRLEN];
  ::inet_ntop(AF_INET, &from.sin_addr, hostBuf, sizeof(hostBuf));
  Endpoint sender{hostBuf, ntohs(from.sin_port)};

  return std::make_pair(std::vector<uint8_t>(buf, buf + n), sender);
}

}  // namespace minislot
