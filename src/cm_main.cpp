#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

#include "minislot/CmStateMachine.hpp"
#include "minislot/Message.hpp"
#include "minislot/UdpSocket.hpp"

using namespace minislot;
using namespace std::chrono_literals;

namespace {
std::atomic<bool> g_running{true};
void handleSignal(int) { g_running = false; }
}  // namespace

// A simulated cable modem. Single-threaded by design (a real CM is one
// device, not a thread pool) - the concurrency in this project lives in
// the CMTS, which genuinely has to handle many CMs at once.
int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <cmts-host> <cmts-port> [label] [requested-bytes-per-sec]\n", argv[0]);
    return 1;
  }
  std::string cmtsHost = argv[1];
  uint16_t cmtsPort = static_cast<uint16_t>(std::atoi(argv[2]));
  std::string label = (argc > 3) ? argv[3] : "cm";
  uint32_t requestedBytesPerSec = (argc > 4) ? static_cast<uint32_t>(std::atoll(argv[4])) : 1'000'000;

  std::setvbuf(stdout, nullptr, _IOLBF, 0);  // line-buffer even when stdout is redirected to a file/pipe
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  UdpSocket socket(/*bindPort=*/0);  // ephemeral port, like a real CM's dynamic client port
  Endpoint cmts{cmtsHost, cmtsPort};

  CmStateMachine stateMachine;
  uint32_t sequence = 0;
  CmState lastLogged = stateMachine.state();

  std::printf("[%s] starting, local port %u\n", label.c_str(), socket.localPort());

  auto lastRequestSentAt = std::chrono::steady_clock::now() - 10s;  // force an immediate first send

  while (g_running && !stateMachine.isTerminal()) {
    auto now = std::chrono::steady_clock::now();

    // (Re)send a request for the current state roughly once a second if
    // we haven't heard back - a crude retry, standing in for DOCSIS's
    // real T-timers (T1-T4) without reimplementing all of them.
    if (now - lastRequestSentAt > 1s) {
      auto req = stateMachine.buildOutgoingRequest(sequence++);
      if (req) {
        if (req->type == MessageType::RegReq) {
          req->requestedUpstreamBytesPerSec = requestedBytesPerSec;
        }
        socket.sendTo(cmts, req->serialize());
        std::printf("[%s] -> %s\n", label.c_str(), toString(req->type));
      }
      lastRequestSentAt = now;
    }

    auto result = socket.receive(/*timeoutMs=*/200);
    if (result) {
      auto [bytes, from] = *result;
      auto frame = MacFrame::deserialize(bytes.data(), bytes.size());
      if (frame) {
        if (frame->type == MessageType::Map) {
          for (uint8_t i = 0; i < frame->numGrants; ++i) {
            if (frame->grants[i].cmId == stateMachine.cmId()) {
              std::printf("[%s] MAP interval=%u got grant: %u slots starting at %u\n", label.c_str(),
                          frame->mapIntervalId, frame->grants[i].numSlots, frame->grants[i].startSlot);
            }
          }
        } else {
          bool changed = stateMachine.onFrameReceived(*frame);
          if (changed) lastRequestSentAt -= 10s;  // don't wait for the retry timer - advance now
        }
      }
    }

    if (stateMachine.state() != lastLogged) {
      std::printf("[%s] state -> %s (cmId=%u)\n", label.c_str(), toString(stateMachine.state()),
                  stateMachine.cmId());
      lastLogged = stateMachine.state();
    }
  }

  if (stateMachine.isTerminal()) {
    std::printf("[%s] FAILED - exiting\n", label.c_str());
    return 2;
  }

  // Operational: keep listening for MAPs until told to stop.
  while (g_running) {
    auto result = socket.receive(/*timeoutMs=*/500);
    if (!result) continue;
    auto [bytes, from] = *result;
    auto frame = MacFrame::deserialize(bytes.data(), bytes.size());
    if (frame && frame->type == MessageType::Map) {
      for (uint8_t i = 0; i < frame->numGrants; ++i) {
        if (frame->grants[i].cmId == stateMachine.cmId()) {
          std::printf("[%s] MAP interval=%u got grant: %u slots starting at %u\n", label.c_str(),
                      frame->mapIntervalId, frame->grants[i].numSlots, frame->grants[i].startSlot);
        }
      }
    }
  }

  std::printf("[%s] shut down cleanly\n", label.c_str());
  return 0;
}
