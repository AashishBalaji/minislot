#include "minislot/CmtsScheduler.hpp"

#include <algorithm>
#include <numeric>

namespace minislot {

namespace {
constexpr int kRangingRoundsBeforeSuccess = 2;  // exercises the "Continue" path realistically
constexpr uint32_t kMinGrantSlots = 2;
}  // namespace

std::vector<MiniSlotGrant> ProportionalFairStrategy::allocate(const std::vector<RegisteredCm>& cms) {
  std::vector<MiniSlotGrant> grants;
  if (cms.empty()) return grants;

  uint64_t totalRequested = 0;
  for (const auto& cm : cms) totalRequested += cm.requestedBytesPerSec;

  // Reserve a floor for every CM first, then distribute the remainder
  // proportionally to requested bandwidth. This is what keeps a
  // low-bandwidth CM from being starved to zero by a greedy one.
  uint32_t floorTotal = static_cast<uint32_t>(cms.size()) * kMinGrantSlots;
  uint32_t remaining = (floorTotal < kTotalUpstreamSlots) ? (kTotalUpstreamSlots - floorTotal) : 0;

  uint32_t cursor = 0;
  for (const auto& cm : cms) {
    uint32_t share = kMinGrantSlots;
    if (totalRequested > 0 && remaining > 0) {
      share += static_cast<uint32_t>(
          (static_cast<uint64_t>(cm.requestedBytesPerSec) * remaining) / totalRequested);
    }
    if (cursor + share > kTotalUpstreamSlots) {
      share = (cursor < kTotalUpstreamSlots) ? (kTotalUpstreamSlots - cursor) : 0;
    }
    if (share == 0) continue;

    MiniSlotGrant g;
    g.cmId = cm.cmId;
    g.startSlot = cursor;
    g.numSlots = share;
    grants.push_back(g);
    cursor += share;
  }
  return grants;
}

CmtsScheduler::CmtsScheduler(std::unique_ptr<AllocationStrategy> strategy)
    : strategy_(std::move(strategy)) {}

std::string CmtsScheduler::peerKey(const Endpoint& ep) {
  return ep.host + ":" + std::to_string(ep.port);
}

MacFrame CmtsScheduler::handleRangingRequest(const MacFrame& req, const Endpoint& from) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string key = peerKey(from);
  int& rounds = rangingRoundsByPeer_[key];
  rounds++;

  MacFrame rsp;
  rsp.type = MessageType::RngRsp;
  rsp.sequence = req.sequence;
  rsp.timingOffsetUs = 0;   // a real CMTS would compute an actual timing adjustment here
  rsp.powerOffsetDb = 0;
  rsp.rangingStatus = (rounds >= kRangingRoundsBeforeSuccess) ? RangingStatus::Success
                                                               : RangingStatus::Continue;
  return rsp;
}

MacFrame CmtsScheduler::handleRegistrationRequest(const MacFrame& req, const Endpoint& from) {
  std::lock_guard<std::mutex> lock(mutex_);

  MacFrame rsp;
  rsp.type = MessageType::RegRsp;
  rsp.sequence = req.sequence;

  // A real CMTS might reject for admission-control reasons (no capacity
  // left); this demo always accepts, which keeps the fairness-allocation
  // logic the interesting part to test rather than admission policy.
  uint32_t assignedId = nextCmId_++;
  RegisteredCm cm;
  cm.cmId = assignedId;
  cm.endpoint = from;
  cm.requestedBytesPerSec = req.requestedUpstreamBytesPerSec;
  registeredCms_[assignedId] = cm;

  rsp.registrationStatus = RegistrationStatus::Accepted;
  rsp.assignedCmId = assignedId;
  return rsp;
}

MacFrame CmtsScheduler::buildMap(uint32_t mapIntervalId) {
  std::vector<RegisteredCm> cms;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cms.reserve(registeredCms_.size());
    for (const auto& [id, cm] : registeredCms_) cms.push_back(cm);
  }

  auto grants = strategy_->allocate(cms);

  MacFrame map;
  map.type = MessageType::Map;
  map.mapIntervalId = mapIntervalId;
  map.numGrants = static_cast<uint8_t>(std::min(grants.size(), kMaxGrantsPerMap));
  for (std::size_t i = 0; i < map.numGrants; ++i) map.grants[i] = grants[i];
  return map;
}

std::size_t CmtsScheduler::registeredCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return registeredCms_.size();
}

std::vector<RegisteredCm> CmtsScheduler::snapshotRegisteredCms() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<RegisteredCm> out;
  out.reserve(registeredCms_.size());
  for (const auto& [id, cm] : registeredCms_) out.push_back(cm);
  return out;
}

}  // namespace minislot
