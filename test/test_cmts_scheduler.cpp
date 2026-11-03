#include <gtest/gtest.h>

#include "minislot/CmtsScheduler.hpp"

using namespace minislot;

namespace {
CmtsScheduler makeScheduler() {
  return CmtsScheduler(std::make_unique<ProportionalFairStrategy>());
}
}  // namespace

TEST(CmtsScheduler, RangingTakesTwoRoundsThenSucceeds) {
  auto scheduler = makeScheduler();
  Endpoint peer{"127.0.0.1", 5000};

  MacFrame req;
  req.type = MessageType::RngReq;

  auto rsp1 = scheduler.handleRangingRequest(req, peer);
  EXPECT_EQ(rsp1.rangingStatus, RangingStatus::Continue);

  auto rsp2 = scheduler.handleRangingRequest(req, peer);
  EXPECT_EQ(rsp2.rangingStatus, RangingStatus::Success);
}

TEST(CmtsScheduler, DifferentPeersTrackRangingRoundsIndependently) {
  auto scheduler = makeScheduler();
  Endpoint peerA{"127.0.0.1", 5000};
  Endpoint peerB{"127.0.0.1", 5001};
  MacFrame req;
  req.type = MessageType::RngReq;

  scheduler.handleRangingRequest(req, peerA);  // A: round 1
  auto rspB1 = scheduler.handleRangingRequest(req, peerB);  // B: round 1
  EXPECT_EQ(rspB1.rangingStatus, RangingStatus::Continue);

  auto rspA2 = scheduler.handleRangingRequest(req, peerA);  // A: round 2 -> success
  EXPECT_EQ(rspA2.rangingStatus, RangingStatus::Success);
}

TEST(CmtsScheduler, RegistrationAssignsIncreasingUniqueIds) {
  auto scheduler = makeScheduler();
  Endpoint peerA{"127.0.0.1", 5000};
  Endpoint peerB{"127.0.0.1", 5001};
  MacFrame req;
  req.type = MessageType::RegReq;
  req.requestedUpstreamBytesPerSec = 1000;

  auto rspA = scheduler.handleRegistrationRequest(req, peerA);
  auto rspB = scheduler.handleRegistrationRequest(req, peerB);

  EXPECT_NE(rspA.assignedCmId, rspB.assignedCmId);
  EXPECT_EQ(scheduler.registeredCount(), 2u);
}

TEST(CmtsScheduler, MapGrantsCoverAllRegisteredCms) {
  auto scheduler = makeScheduler();
  MacFrame req;
  req.type = MessageType::RegReq;
  req.requestedUpstreamBytesPerSec = 1000;

  scheduler.handleRegistrationRequest(req, Endpoint{"127.0.0.1", 5000});
  scheduler.handleRegistrationRequest(req, Endpoint{"127.0.0.1", 5001});
  scheduler.handleRegistrationRequest(req, Endpoint{"127.0.0.1", 5002});

  auto map = scheduler.buildMap(0);
  EXPECT_EQ(map.numGrants, 3);

  uint32_t totalSlots = 0;
  for (uint8_t i = 0; i < map.numGrants; ++i) totalSlots += map.grants[i].numSlots;
  EXPECT_LE(totalSlots, kTotalUpstreamSlots);
}

TEST(CmtsScheduler, ProportionalFairGivesMoreSlotsToHigherRequest) {
  auto scheduler = makeScheduler();
  MacFrame lowReq;
  lowReq.type = MessageType::RegReq;
  lowReq.requestedUpstreamBytesPerSec = 1000;

  MacFrame highReq;
  highReq.type = MessageType::RegReq;
  highReq.requestedUpstreamBytesPerSec = 9000;

  auto rspLow = scheduler.handleRegistrationRequest(lowReq, Endpoint{"127.0.0.1", 5000});
  auto rspHigh = scheduler.handleRegistrationRequest(highReq, Endpoint{"127.0.0.1", 5001});

  auto map = scheduler.buildMap(0);
  ASSERT_EQ(map.numGrants, 2);

  uint32_t lowSlots = 0, highSlots = 0;
  for (uint8_t i = 0; i < map.numGrants; ++i) {
    if (map.grants[i].cmId == rspLow.assignedCmId) lowSlots = map.grants[i].numSlots;
    if (map.grants[i].cmId == rspHigh.assignedCmId) highSlots = map.grants[i].numSlots;
  }
  EXPECT_GT(highSlots, lowSlots);
}

TEST(CmtsScheduler, EvenTheLowestRequesterGetsAFloorGrant) {
  // Proportional-fair without a floor can starve a very-low-bandwidth CM
  // to zero slots when a much larger request dominates the pool. The
  // floor in ProportionalFairStrategy::allocate exists specifically to
  // prevent that - this test is what would catch it regressing.
  auto scheduler = makeScheduler();
  MacFrame tinyReq;
  tinyReq.type = MessageType::RegReq;
  tinyReq.requestedUpstreamBytesPerSec = 1;  // vanishingly small vs. the other CM

  MacFrame hugeReq;
  hugeReq.type = MessageType::RegReq;
  hugeReq.requestedUpstreamBytesPerSec = 1'000'000;

  auto rspTiny = scheduler.handleRegistrationRequest(tinyReq, Endpoint{"127.0.0.1", 5000});
  scheduler.handleRegistrationRequest(hugeReq, Endpoint{"127.0.0.1", 5001});

  auto map = scheduler.buildMap(0);
  uint32_t tinySlots = 0;
  for (uint8_t i = 0; i < map.numGrants; ++i) {
    if (map.grants[i].cmId == rspTiny.assignedCmId) tinySlots = map.grants[i].numSlots;
  }
  EXPECT_GT(tinySlots, 0u);
}

TEST(CmtsScheduler, NoRegisteredCmsProducesEmptyMap) {
  auto scheduler = makeScheduler();
  auto map = scheduler.buildMap(0);
  EXPECT_EQ(map.numGrants, 0);
}
