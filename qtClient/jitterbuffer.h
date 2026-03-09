#ifndef JITTERBUFFER_H
#define JITTERBUFFER_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "rtc/rtc.hpp"

class jitterbuffer
{
    std::map<std::int16_t, std::vector<std::byte>> _data;

    std::int64_t decoding_started_ts = 0;

    std::uint16_t firstSeqNum = 0;
    std::uint16_t lenght = 0;
    bool isFirstPresent = false;
    bool isLastPresent = false;
    bool isFormed = false;

public:
    jitterbuffer() {};
    std::vector<std::byte> addVp8Packet(std::vector<std::byte> pkg, std::int16_t prevMarkedPkg);

    // should be used only if addPacket returned frame, i.e. frame is formed
    bool isKeyFrame();
    std::vector<std::uint32_t> getPacketsToNack();
};

#endif // JITTERBUFFER_H
