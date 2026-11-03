#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace minislot {

// A simplified but semantically real subset of DOCSIS MAC management
// messages. Real DOCSIS defines many more (DCC, DBC, UCD, TRC-REQ...) and
// a much richer TLV encoding; this keeps the message *roles* and the
// *protocol flow* genuine (ranging -> registration -> periodic bandwidth
// allocation) while using a compact fixed-layout wire format instead of
// full DOCSIS TLV encoding, which would be weeks of spec-reading for no
// additional design-pattern or systems value in a portfolio project.
enum class MessageType : uint8_t {
  RngReq = 1,  // CM -> CMTS: "I'm here, help me calibrate timing/power"
  RngRsp = 2,  // CMTS -> CM: timing/power offset + ranging status
  RegReq = 3,  // CM -> CMTS: "Let me onto the network, here's my capability"
  RegRsp = 4,  // CMTS -> CM: registration accepted/rejected + assigned CM id
  Map = 5,     // CMTS -> all CMs (broadcast): upstream minislot allocation for this interval
  Sync = 6,    // CMTS -> all CMs (broadcast): timing reference tick
};

enum class RangingStatus : uint8_t { Continue = 0, Success = 1, Abort = 2 };
enum class RegistrationStatus : uint8_t { Accepted = 0, Rejected = 1 };

constexpr std::size_t kMaxGrantsPerMap = 32;

// One CM's allocated upstream transmission opportunity within a MAP interval.
struct MiniSlotGrant {
  uint32_t cmId = 0;
  uint32_t startSlot = 0;
  uint32_t numSlots = 0;
};

// Wire-format MAC frame. Fixed layout, network byte order for multi-byte
// fields, no padding games - deliberately simple to parse so the test
// suite can focus on protocol *behavior* (state transitions, fairness)
// rather than on exercising a bespoke encoder.
struct MacFrame {
  MessageType type{};
  uint32_t cmId = 0;       // sender's CM id (0 = not yet assigned)
  uint32_t sequence = 0;   // per-CM monotonic sequence number

  // RNG-REQ / RNG-RSP
  int32_t timingOffsetUs = 0;
  int32_t powerOffsetDb = 0;
  RangingStatus rangingStatus = RangingStatus::Continue;

  // REG-REQ / REG-RSP
  uint32_t requestedUpstreamBytesPerSec = 0;
  RegistrationStatus registrationStatus = RegistrationStatus::Accepted;
  uint32_t assignedCmId = 0;

  // MAP
  uint32_t mapIntervalId = 0;
  uint8_t numGrants = 0;
  MiniSlotGrant grants[kMaxGrantsPerMap]{};

  // SYNC
  uint64_t masterClockTicks = 0;

  std::vector<uint8_t> serialize() const;
  static std::optional<MacFrame> deserialize(const uint8_t* data, std::size_t len);
};

const char* toString(MessageType type);

}  // namespace minislot
