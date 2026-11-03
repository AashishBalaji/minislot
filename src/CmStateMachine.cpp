#include "minislot/CmStateMachine.hpp"

namespace minislot {

const char* toString(CmState state) {
  switch (state) {
    case CmState::Init: return "Init";
    case CmState::Ranging: return "Ranging";
    case CmState::Registering: return "Registering";
    case CmState::Operational: return "Operational";
    case CmState::Failed: return "Failed";
  }
  return "Unknown";
}

std::optional<MacFrame> CmStateMachine::buildOutgoingRequest(uint32_t sequence) {
  switch (state_) {
    case CmState::Init: {
      state_ = CmState::Ranging;
      [[fallthrough]];
    }
    case CmState::Ranging: {
      MacFrame f;
      f.type = MessageType::RngReq;
      f.cmId = cmId_;
      f.sequence = sequence;
      return f;
    }
    case CmState::Registering: {
      MacFrame f;
      f.type = MessageType::RegReq;
      f.cmId = cmId_;
      f.sequence = sequence;
      f.requestedUpstreamBytesPerSec = 1'000'000;  // fixed demo request; a real CM would size this to its service tier
      return f;
    }
    case CmState::Operational:
    case CmState::Failed:
      return std::nullopt;
  }
  return std::nullopt;
}

bool CmStateMachine::onFrameReceived(const MacFrame& frame) {
  switch (state_) {
    case CmState::Init:
      return false;  // shouldn't receive anything before our first request goes out

    case CmState::Ranging:
      if (frame.type != MessageType::RngRsp) return false;
      switch (frame.rangingStatus) {
        case RangingStatus::Continue:
          return false;  // CMTS wants another ranging round - stay in Ranging, resend RNG-REQ
        case RangingStatus::Success:
          state_ = CmState::Registering;
          return true;
        case RangingStatus::Abort:
          state_ = CmState::Failed;
          return true;
      }
      return false;

    case CmState::Registering:
      if (frame.type != MessageType::RegRsp) return false;
      if (frame.registrationStatus == RegistrationStatus::Accepted) {
        cmId_ = frame.assignedCmId;
        state_ = CmState::Operational;
      } else {
        state_ = CmState::Failed;
      }
      return true;

    case CmState::Operational:
      // MAP/SYNC frames are consumed by the caller for scheduling info;
      // they don't change CmState once operational.
      return false;

    case CmState::Failed:
      return false;
  }
  return false;
}

}  // namespace minislot
