#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "minislot/Message.hpp"
#include "minislot/UdpSocket.hpp"

namespace minislot {

struct RegisteredCm {
  uint32_t cmId = 0;
  Endpoint endpoint;
  uint32_t requestedBytesPerSec = 0;
  int rangingRounds = 0;
};

constexpr uint32_t kTotalUpstreamSlots = 1000;  // per MAP interval, arbitrary demo capacity

// Strategy interface for turning a set of registered CMs' requested
// bandwidth into concrete minislot grants. Separated from CmtsScheduler so
// a different fairness policy (strict round-robin, weighted by service
// tier, deficit round-robin...) can be swapped in without touching
// registration/ranging logic - the actual point of the Strategy pattern
// here, not just using the word in a README.
class AllocationStrategy {
 public:
  virtual ~AllocationStrategy() = default;
  virtual std::vector<MiniSlotGrant> allocate(const std::vector<RegisteredCm>& cms) = 0;
};

// Proportional-fair: each CM's share of the kTotalUpstreamSlots is
// proportional to its requested bytes/sec, with a minimum floor so a CM
// that asked for very little still gets some opportunity to transmit
// (DOCSIS CMTSes do something conceptually similar via QoS service flows;
// this is a deliberately simplified stand-in for that).
class ProportionalFairStrategy : public AllocationStrategy {
 public:
  std::vector<MiniSlotGrant> allocate(const std::vector<RegisteredCm>& cms) override;
};

// Manages the registered-CM table and produces MAP frames. Thread-safe:
// the receive-handling thread(s) update registration state while the
// scheduler thread reads it to build each MAP, so all access goes through
// mutex_.
class CmtsScheduler {
 public:
  explicit CmtsScheduler(std::unique_ptr<AllocationStrategy> strategy);

  // Returns the next ranging response to send: Continue for the first N
  // rounds (to exercise that path realistically), then Success.
  MacFrame handleRangingRequest(const MacFrame& req, const Endpoint& from);

  // Assigns a CM id and registers the CM's requested bandwidth.
  MacFrame handleRegistrationRequest(const MacFrame& req, const Endpoint& from);

  // Builds the next MAP frame covering all currently registered CMs.
  MacFrame buildMap(uint32_t mapIntervalId);

  std::size_t registeredCount() const;
  std::vector<RegisteredCm> snapshotRegisteredCms() const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<uint32_t, RegisteredCm> registeredCms_;  // keyed by assigned cmId
  std::unordered_map<std::string, int> rangingRoundsByPeer_;  // keyed by "host:port" before a cmId exists
  uint32_t nextCmId_ = 100;
  std::unique_ptr<AllocationStrategy> strategy_;

  static std::string peerKey(const Endpoint& ep);
};

}  // namespace minislot
