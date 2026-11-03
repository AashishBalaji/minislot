#pragma once

#include <functional>
#include <string>

#include "minislot/Message.hpp"

namespace minislot {

// CM lifecycle, modeled on the real DOCSIS boot sequence (simplified -
// real DOCSIS also has DS/US channel scanning and DHCP/TFTP provisioning
// stages before ranging, which add nothing to the state-machine design
// story here and were left out deliberately).
enum class CmState {
  Init,          // just started, about to send first RNG-REQ
  Ranging,       // sent RNG-REQ, waiting for RNG-RSP; may repeat on "Continue"
  Registering,   // ranging succeeded, sent REG-REQ, waiting for REG-RSP
  Operational,   // registered; sending data, listening for MAP/SYNC
  Failed,        // ranging aborted or registration rejected - terminal
};

const char* toString(CmState state);

// A small explicit state machine (the State pattern via a switch rather
// than a class hierarchy - appropriate here since there are only five
// states and the transition logic is simple enough that a class-per-state
// would be more indirection than it buys back). Pure logic, no I/O, so it
// is unit-testable without a socket or a running CMTS.
class CmStateMachine {
 public:
  explicit CmStateMachine(uint32_t initialCmId = 0) : cmId_(initialCmId) {}

  CmState state() const { return state_; }
  uint32_t cmId() const { return cmId_; }

  // Called when it's time to (re)send a request for the current state.
  // Returns the frame to send, or std::nullopt if nothing to send right now
  // (e.g. Operational just listens).
  std::optional<MacFrame> buildOutgoingRequest(uint32_t sequence);

  // Feeds a received frame into the state machine. Returns true if the
  // frame caused a state transition.
  bool onFrameReceived(const MacFrame& frame);

  bool isTerminal() const { return state_ == CmState::Failed; }

 private:
  CmState state_ = CmState::Init;
  uint32_t cmId_;
};

}  // namespace minislot
