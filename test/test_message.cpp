#include <gtest/gtest.h>

#include "minislot/Message.hpp"

using namespace minislot;

TEST(MessageSerialization, RoundTripsRngReq) {
  MacFrame f;
  f.type = MessageType::RngReq;
  f.cmId = 42;
  f.sequence = 7;

  auto bytes = f.serialize();
  auto decoded = MacFrame::deserialize(bytes.data(), bytes.size());

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->type, MessageType::RngReq);
  EXPECT_EQ(decoded->cmId, 42u);
  EXPECT_EQ(decoded->sequence, 7u);
}

TEST(MessageSerialization, RoundTripsMapWithGrants) {
  MacFrame f;
  f.type = MessageType::Map;
  f.mapIntervalId = 5;
  f.numGrants = 3;
  f.grants[0] = {100, 0, 10};
  f.grants[1] = {101, 10, 20};
  f.grants[2] = {102, 30, 5};

  auto bytes = f.serialize();
  auto decoded = MacFrame::deserialize(bytes.data(), bytes.size());

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->mapIntervalId, 5u);
  EXPECT_EQ(decoded->numGrants, 3);
  EXPECT_EQ(decoded->grants[1].cmId, 101u);
  EXPECT_EQ(decoded->grants[1].startSlot, 10u);
  EXPECT_EQ(decoded->grants[1].numSlots, 20u);
}

TEST(MessageSerialization, RoundTripsNegativeOffsets) {
  // timingOffsetUs / powerOffsetDb are signed - a modem can be early or
  // late, over-powered or under-powered. This guards the int32 cast path.
  MacFrame f;
  f.type = MessageType::RngRsp;
  f.timingOffsetUs = -1500;
  f.powerOffsetDb = -3;

  auto bytes = f.serialize();
  auto decoded = MacFrame::deserialize(bytes.data(), bytes.size());

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->timingOffsetUs, -1500);
  EXPECT_EQ(decoded->powerOffsetDb, -3);
}

TEST(MessageSerialization, RejectsTruncatedBuffer) {
  MacFrame f;
  f.type = MessageType::RegReq;
  auto bytes = f.serialize();
  bytes.resize(bytes.size() - 3);  // cut it short

  auto decoded = MacFrame::deserialize(bytes.data(), bytes.size());
  EXPECT_FALSE(decoded.has_value());
}

TEST(MessageSerialization, RejectsUnknownMessageType) {
  std::vector<uint8_t> bytes = {99};  // not a valid MessageType value
  auto decoded = MacFrame::deserialize(bytes.data(), bytes.size());
  EXPECT_FALSE(decoded.has_value());
}

TEST(MessageSerialization, RejectsGrantCountBeyondCapacity) {
  // Hand-crafted malformed frame: numGrants claims more grants than the
  // wire format allows for, and the buffer isn't long enough to back that
  // claim either way. This is exactly the kind of input a fuzzer or a
  // misbehaving peer would send; deserialize() must refuse it, not read
  // past the array bound.
  std::vector<uint8_t> bytes = {
      static_cast<uint8_t>(MessageType::Map),
      0, 0, 0, 0,  // cmId
      0, 0, 0, 0,  // sequence
      0, 0, 0, 0,  // timingOffsetUs
      0, 0, 0, 0,  // powerOffsetDb
      0,           // rangingStatus
      0, 0, 0, 0,  // requestedUpstreamBytesPerSec
      0,           // registrationStatus
      0, 0, 0, 0,  // assignedCmId
      0, 0, 0, 0,  // mapIntervalId
      200,         // numGrants - far beyond kMaxGrantsPerMap(32)
  };
  auto decoded = MacFrame::deserialize(bytes.data(), bytes.size());
  EXPECT_FALSE(decoded.has_value());
}
