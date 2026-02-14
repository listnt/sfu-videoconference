#include "rtppipe.h"

void ProcessPipe::write(rtc::binary frame)
{
    auto fragments = this->packetizer->fragment(frame);

    for (auto fragment : fragments) {
        auto packet = this->packetizer->packetize(fragment, true);
        this->process->write(reinterpret_cast<const char *>(packet->data()), packet->size());
        this->process->waitForBytesWritten();
    }
}
