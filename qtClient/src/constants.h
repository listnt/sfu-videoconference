#ifndef CONSTANTS_H
#define CONSTANTS_H
#define NACK_TIMEOUT_MS 30
#define NACK_MAX_TRIES 10

#include <string>

namespace MyApp {
const std::string AppName = "stupid_shit";
const std::string VP8CODEC = "VP8";
const std::string VP9CODEC = "VP9";
const std::string Rtx = "rtx";
const std::string OpusCodec = "opus";
} // namespace MyApp

#endif // CONSTANTS_H
