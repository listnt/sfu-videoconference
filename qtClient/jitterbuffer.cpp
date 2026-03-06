#include "jitterbuffer.h"

std::vector<std::byte> jitterbuffer::addPacket(
    std::vector<std::byte> pkg, std::int16_t prevMarkedPkg) {
  if (this->decoding_started_ts == 0) {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    this->decoding_started_ts =
        std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  }

  // First byte
  const uint8_t X = 0b10000000;
  // const uint8_t N = 0b00100000;
  const uint8_t S = 0b00010000;

  // Extension byte
  const uint8_t I = 0b10000000;
  const uint8_t L = 0b01000000;
  const uint8_t T = 0b00100000;
  const uint8_t K = 0b00010000;

  // PictureID byte
  const uint8_t M = 0b10000000;
  const uint8_t pid = 0b00000111;

  if (pkg.empty()) {
    return {};
  }

  auto rtpHeader = reinterpret_cast<const rtc::RtpHeader *>(pkg.data());
  auto rtpHeaderSize = rtpHeader->getSize() + rtpHeader
                                                  ->getExtensionHeaderSize();
  auto paddingSize = 0;
  if (rtpHeader->padding()) {
    paddingSize = uint8_t(pkg.back());
  }

  if (pkg.size() <= rtpHeaderSize + paddingSize) {
    return {}; // Empty payload
  }

  const std::byte *payloadData =
      reinterpret_cast<std::byte *>(pkg.data() + rtpHeaderSize);
  size_t payloadSize = pkg.size() - rtpHeaderSize - paddingSize;

  if (payloadSize < 1) {
    return {};
  }

  size_t descriptorSize = 1;
  uint8_t firstByte = std::to_integer<uint8_t>(payloadData[0]);

  // these bytes only valid if their according bit is set
  // do not, I repeat DO NOT consider them as valid
  uint8_t extensionByte = std::to_integer<uint8_t>(payloadData[1]);
  uint8_t pictureIdByte = std::to_integer<uint8_t>(payloadData[2]);
  uint8_t addPicId = std::to_integer<uint8_t>(payloadData[3]);

  if (firstByte & X) {
    descriptorSize++;

    if (extensionByte & I) {
      descriptorSize += (pictureIdByte & M) ? 2 : 1;
    }
    if (extensionByte & L) {
      descriptorSize++;
    }
    if ((extensionByte & T) || (extensionByte & K)) {
      descriptorSize++;
    }
  }

  if (payloadSize < descriptorSize) {
    return {};
  }

  payloadData += descriptorSize;
  payloadSize -= descriptorSize;

  if (firstByte & S) {
    if ((firstByte & pid) == 0) {
      this->isFirstPresent = true;
      this->firstSeqNum = rtpHeader->seqNumber();
    }
  }

  if (rtpHeader->marker()) {
    this->isLastPresent = true;
    this->lastSeqNum = rtpHeader->seqNumber();
  }

  this->_data[rtpHeader->seqNumber()] = std::
      vector<std::byte>(payloadData, payloadData + payloadSize);

  if (this->isFirstPresent && this->isLastPresent) {
    std::uint16_t dist = this->lastSeqNum - this->firstSeqNum;
    if (this->_data.size() == dist + 1) {
      std::vector<std::byte> res;
      std::int64_t totalSize = 0;

      for (std::uint16_t i = this->firstSeqNum; i != this->lastSeqNum + 1;
           i++) {
        totalSize += this->_data[i].size();
      }

      res.reserve(totalSize);

      for (std::uint16_t i = this->firstSeqNum; i != this->lastSeqNum + 1;
           i++) {
        res.insert(res.end(), this->_data[i].begin(), this->_data[i].end());
      }

      this->isFormed = true;

      return res;
    }
  }

  return {};
}
