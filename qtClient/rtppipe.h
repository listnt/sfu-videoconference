#ifndef RTPPIPE_H
#define RTPPIPE_H

#include <QProcess>
#include <rtc/rtc.h>
#include <rtc/rtppacketizer.hpp>
#include <rtc/vp8rtppacketizer.hpp>

class rtpPipe : public rtc::RtpPacketizer
{
    friend class ProcessPipe;
};

class ProcessPipe
{
private:
    QProcess *process;
    std::shared_ptr<rtpPipe> packetizer;

public:
    void setPacketrizer(std::shared_ptr<rtpPipe> packetizer) { this->packetizer = packetizer; };
    void setProcess(QProcess *process) { this->process = process; };
    void write(rtc::binary frame);
};

#endif // RTPPIPE_H
