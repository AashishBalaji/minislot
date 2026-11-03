#include <gtest/gtest.h>

#include "minislot/CmStateMachine.hpp"

using namespace minislot;

TEST(CmStateMachine, StartsInInit) {
  CmStateMachine sm;
  EXPECT_EQ(sm.state(), CmState::Init);
}

TEST(CmStateMachine, FirstRequestMovesToRanging) {
  CmStateMachine sm;
  auto req = sm.buildOutgoingRequest(0);
  ASSERT_TRUE(req.has_value());
  EXPECT_EQ(req->type, MessageType::RngReq);
  EXPECT_EQ(sm.state(), CmState::Ranging);
}

TEST(CmStateMachine, RangingContinueStaysInRanging) {
  CmStateMachine sm;
  sm.buildOutgoingRequest(0);

  MacFrame rsp;
  rsp.type = MessageType::RngRsp;
  rsp.rangingStatus = RangingStatus::Continue;

  bool changed = sm.onFrameReceived(rsp);
  EXPECT_FALSE(changed);
  EXPECT_EQ(sm.state(), CmState::Ranging);
}

TEST(CmStateMachine, RangingSuccessMovesToRegistering) {
  CmStateMachine sm;
  sm.buildOutgoingRequest(0);

  MacFrame rsp;
  rsp.type = MessageType::RngRsp;
  rsp.rangingStatus = RangingStatus::Success;

  bool changed = sm.onFrameReceived(rsp);
  EXPECT_TRUE(changed);
  EXPECT_EQ(sm.state(), CmState::Registering);
}

TEST(CmStateMachine, RangingAbortMovesToFailedTerminal) {
  CmStateMachine sm;
  sm.buildOutgoingRequest(0);

  MacFrame rsp;
  rsp.type = MessageType::RngRsp;
  rsp.rangingStatus = RangingStatus::Abort;

  sm.onFrameReceived(rsp);
  EXPECT_EQ(sm.state(), CmState::Failed);
  EXPECT_TRUE(sm.isTerminal());
}

TEST(CmStateMachine, RegistrationAcceptedMovesToOperationalAndAdoptsAssignedId) {
  CmStateMachine sm;
  sm.buildOutgoingRequest(0);
  MacFrame rngRsp;
  rngRsp.type = MessageType::RngRsp;
  rngRsp.rangingStatus = RangingStatus::Success;
  sm.onFrameReceived(rngRsp);

  sm.buildOutgoingRequest(1);  // sends REG-REQ

  MacFrame regRsp;
  regRsp.type = MessageType::RegRsp;
  regRsp.registrationStatus = RegistrationStatus::Accepted;
  regRsp.assignedCmId = 101;

  bool changed = sm.onFrameReceived(regRsp);
  EXPECT_TRUE(changed);
  EXPECT_EQ(sm.state(), CmState::Operational);
  EXPECT_EQ(sm.cmId(), 101u);
}

TEST(CmStateMachine, RegistrationRejectedMovesToFailed) {
  CmStateMachine sm;
  sm.buildOutgoingRequest(0);
  MacFrame rngRsp;
  rngRsp.type = MessageType::RngRsp;
  rngRsp.rangingStatus = RangingStatus::Success;
  sm.onFrameReceived(rngRsp);
  sm.buildOutgoingRequest(1);

  MacFrame regRsp;
  regRsp.type = MessageType::RegRsp;
  regRsp.registrationStatus = RegistrationStatus::Rejected;

  sm.onFrameReceived(regRsp);
  EXPECT_EQ(sm.state(), CmState::Failed);
}

TEST(CmStateMachine, OperationalBuildsNoOutgoingRequest) {
  CmStateMachine sm;
  sm.buildOutgoingRequest(0);
  MacFrame rngRsp;
  rngRsp.type = MessageType::RngRsp;
  rngRsp.rangingStatus = RangingStatus::Success;
  sm.onFrameReceived(rngRsp);
  sm.buildOutgoingRequest(1);
  MacFrame regRsp;
  regRsp.type = MessageType::RegRsp;
  regRsp.registrationStatus = RegistrationStatus::Accepted;
  regRsp.assignedCmId = 101;
  sm.onFrameReceived(regRsp);

  auto req = sm.buildOutgoingRequest(2);
  EXPECT_FALSE(req.has_value());
}

TEST(CmStateMachine, WrongMessageTypeForStateIsIgnored) {
  // A REG-RSP arriving while still in Ranging (e.g. a stale/reordered
  // packet) must not be treated as a ranging response.
  CmStateMachine sm;
  sm.buildOutgoingRequest(0);

  MacFrame regRsp;
  regRsp.type = MessageType::RegRsp;
  regRsp.registrationStatus = RegistrationStatus::Accepted;

  bool changed = sm.onFrameReceived(regRsp);
  EXPECT_FALSE(changed);
  EXPECT_EQ(sm.state(), CmState::Ranging);
}
