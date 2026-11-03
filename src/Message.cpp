#include "minislot/Message.hpp"

#include <arpa/inet.h>

namespace minislot {

namespace {

void putU32(std::vector<uint8_t>& buf, uint32_t v) {
  uint32_t n = htonl(v);
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&n);
  buf.insert(buf.end(), p, p + 4);
}

void putU64(std::vector<uint8_t>& buf, uint64_t v) {
  uint32_t hi = htonl(static_cast<uint32_t>(v >> 32));
  uint32_t lo = htonl(static_cast<uint32_t>(v & 0xFFFFFFFFu));
  putU32(buf, ntohl(hi));
  putU32(buf, ntohl(lo));
}

void putU8(std::vector<uint8_t>& buf, uint8_t v) { buf.push_back(v); }

bool getU32(const uint8_t* data, std::size_t len, std::size_t& offset, uint32_t& out) {
  if (offset + 4 > len) return false;
  uint32_t n;
  std::memcpy(&n, data + offset, 4);
  out = ntohl(n);
  offset += 4;
  return true;
}

bool getU64(const uint8_t* data, std::size_t len, std::size_t& offset, uint64_t& out) {
  uint32_t hi, lo;
  if (!getU32(data, len, offset, hi)) return false;
  if (!getU32(data, len, offset, lo)) return false;
  out = (static_cast<uint64_t>(hi) << 32) | lo;
  return true;
}

bool getU8(const uint8_t* data, std::size_t len, std::size_t& offset, uint8_t& out) {
  if (offset + 1 > len) return false;
  out = data[offset];
  offset += 1;
  return true;
}

}  // namespace

std::vector<uint8_t> MacFrame::serialize() const {
  std::vector<uint8_t> buf;
  buf.reserve(64);
  putU8(buf, static_cast<uint8_t>(type));
  putU32(buf, cmId);
  putU32(buf, sequence);

  putU32(buf, static_cast<uint32_t>(timingOffsetUs));
  putU32(buf, static_cast<uint32_t>(powerOffsetDb));
  putU8(buf, static_cast<uint8_t>(rangingStatus));

  putU32(buf, requestedUpstreamBytesPerSec);
  putU8(buf, static_cast<uint8_t>(registrationStatus));
  putU32(buf, assignedCmId);

  putU32(buf, mapIntervalId);
  putU8(buf, numGrants);
  for (uint8_t i = 0; i < numGrants && i < kMaxGrantsPerMap; ++i) {
    putU32(buf, grants[i].cmId);
    putU32(buf, grants[i].startSlot);
    putU32(buf, grants[i].numSlots);
  }

  putU64(buf, masterClockTicks);
  return buf;
}

std::optional<MacFrame> MacFrame::deserialize(const uint8_t* data, std::size_t len) {
  MacFrame f;
  std::size_t off = 0;
  uint8_t typeByte, rangingByte, regByte, numGrants;
  uint32_t timingRaw, powerRaw;

  if (!getU8(data, len, off, typeByte)) return std::nullopt;
  if (typeByte < 1 || typeByte > 6) return std::nullopt;
  f.type = static_cast<MessageType>(typeByte);

  if (!getU32(data, len, off, f.cmId)) return std::nullopt;
  if (!getU32(data, len, off, f.sequence)) return std::nullopt;

  if (!getU32(data, len, off, timingRaw)) return std::nullopt;
  f.timingOffsetUs = static_cast<int32_t>(timingRaw);
  if (!getU32(data, len, off, powerRaw)) return std::nullopt;
  f.powerOffsetDb = static_cast<int32_t>(powerRaw);
  if (!getU8(data, len, off, rangingByte)) return std::nullopt;
  f.rangingStatus = static_cast<RangingStatus>(rangingByte);

  if (!getU32(data, len, off, f.requestedUpstreamBytesPerSec)) return std::nullopt;
  if (!getU8(data, len, off, regByte)) return std::nullopt;
  f.registrationStatus = static_cast<RegistrationStatus>(regByte);
  if (!getU32(data, len, off, f.assignedCmId)) return std::nullopt;

  if (!getU32(data, len, off, f.mapIntervalId)) return std::nullopt;
  if (!getU8(data, len, off, numGrants)) return std::nullopt;
  f.numGrants = numGrants;
  for (uint8_t i = 0; i < numGrants; ++i) {
    if (i >= kMaxGrantsPerMap) return std::nullopt;  // malformed: more grants than the format allows
    if (!getU32(data, len, off, f.grants[i].cmId)) return std::nullopt;
    if (!getU32(data, len, off, f.grants[i].startSlot)) return std::nullopt;
    if (!getU32(data, len, off, f.grants[i].numSlots)) return std::nullopt;
  }

  if (!getU64(data, len, off, f.masterClockTicks)) return std::nullopt;
  return f;
}

const char* toString(MessageType type) {
  switch (type) {
    case MessageType::RngReq: return "RNG-REQ";
    case MessageType::RngRsp: return "RNG-RSP";
    case MessageType::RegReq: return "REG-REQ";
    case MessageType::RegRsp: return "REG-RSP";
    case MessageType::Map: return "MAP";
    case MessageType::Sync: return "SYNC";
  }
  return "UNKNOWN";
}

}  // namespace minislot
