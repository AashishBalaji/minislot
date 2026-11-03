#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>
#include <vector>

#include "minislot/CmtsScheduler.hpp"
#include "minislot/Message.hpp"
#include "minislot/ThreadSafeQueue.hpp"
#include "minislot/UdpSocket.hpp"

using namespace minislot;
using namespace std::chrono_literals;

namespace {
std::atomic<bool> g_running{true};
void handleSignal(int) { g_running = false; }

struct Inbound {
  MacFrame frame;
  Endpoint from;
};
}  // namespace

// Three threads, each with one job - a deliberately clean
// producer-consumer / scheduler split rather than one loop doing
// everything, so each piece is independently testable and so a slow
// handler can never stall the socket:
//   1. receiveLoop   - owns the socket, does nothing but recv() + enqueue
//   2. workerLoop    - dequeues frames, runs ranging/registration logic,
//                      sends the response
//   3. mapLoop       - on a fixed tick, builds a MAP from current
//                      registration state and broadcasts it
int main(int argc, char** argv) {
  uint16_t port = (argc > 1) ? static_cast<uint16_t>(std::atoi(argv[1])) : 9000;
  std::setvbuf(stdout, nullptr, _IOLBF, 0);  // line-buffer even when stdout is redirected to a file/pipe
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  UdpSocket socket(port);
  std::printf("[cmts] listening on UDP port %u\n", socket.localPort());

  CmtsScheduler scheduler(std::make_unique<ProportionalFairStrategy>());
  ThreadSafeQueue<Inbound> inbox;

  std::thread receiveThread([&] {
    while (g_running) {
      auto result = socket.receive(/*timeoutMs=*/200);
      if (!result) continue;
      auto [bytes, from] = *result;
      auto frame = MacFrame::deserialize(bytes.data(), bytes.size());
      if (!frame) {
        std::fprintf(stderr, "[cmts] dropped malformed frame from %s:%u\n", from.host.c_str(), from.port);
        continue;
      }
      inbox.push(Inbound{*frame, from});
    }
    inbox.stop();
  });

  std::thread workerThread([&] {
    while (true) {
      auto item = inbox.pop();
      if (!item) break;  // queue stopped and drained - shutting down
      const auto& [frame, from] = *item;

      switch (frame.type) {
        case MessageType::RngReq: {
          MacFrame rsp = scheduler.handleRangingRequest(frame, from);
          socket.sendTo(from, rsp.serialize());
          std::printf("[cmts] RNG-REQ from %s:%u -> %s\n", from.host.c_str(), from.port,
                      rsp.rangingStatus == RangingStatus::Success ? "RNG-RSP(Success)" : "RNG-RSP(Continue)");
          break;
        }
        case MessageType::RegReq: {
          MacFrame rsp = scheduler.handleRegistrationRequest(frame, from);
          socket.sendTo(from, rsp.serialize());
          std::printf("[cmts] REG-REQ from %s:%u -> assigned cmId=%u\n", from.host.c_str(), from.port,
                      rsp.assignedCmId);
          break;
        }
        default:
          std::fprintf(stderr, "[cmts] unexpected message type from CM: %s\n", toString(frame.type));
      }
    }
  });

  std::thread mapThread([&] {
    uint32_t intervalId = 0;
    while (g_running) {
      std::this_thread::sleep_for(500ms);
      MacFrame map = scheduler.buildMap(intervalId++);
      auto cms = scheduler.snapshotRegisteredCms();
      for (const auto& cm : cms) {
        socket.sendTo(cm.endpoint, map.serialize());
      }
      if (!cms.empty()) {
        std::printf("[cmts] MAP interval=%u grants=%d registered_cms=%zu\n", map.mapIntervalId,
                    map.numGrants, cms.size());
      }
    }
  });

  while (g_running) std::this_thread::sleep_for(100ms);

  mapThread.join();
  inbox.stop();
  receiveThread.join();
  workerThread.join();
  std::printf("[cmts] shut down cleanly\n");
  return 0;
}
