#include "conferenceclient.h"

typedef int SOCKET;

using rtc::binary;
using rtc::string;

// Write 32-bit little-endian
static void write_u32_le(std::stringbuf &ofs, uint32_t v)
{
    char b[4];
    b[0] = static_cast<char>(v & 0xFF);
    b[1] = static_cast<char>((v >> 8) & 0xFF);
    b[2] = static_cast<char>((v >> 16) & 0xFF);
    b[3] = static_cast<char>((v >> 24) & 0xFF);
    ofs.sputn(b, 4);
}

// Write 16-bit little-endian
static void write_u16_le(std::stringbuf &ofs, uint16_t v)
{
    char b[2];
    b[0] = static_cast<char>(v & 0xFF);
    b[1] = static_cast<char>((v >> 8) & 0xFF);
    ofs.sputn(b, 2);
}

// Write IVF file header (32 bytes)
static void write_ivf_file_header(std::stringbuf &ofs,
                                  const char codec[4],
                                  uint16_t width,
                                  uint16_t height,
                                  uint32_t framerate_num,
                                  uint32_t framerate_den,
                                  uint32_t frame_count)
{
    // Signature 'DKIF'
    ofs.sputn("DKIF", 4);
    // Version (2 bytes) and header size (2 bytes) -> version 0, header size 32
    write_u16_le(ofs, 0);
    write_u16_le(ofs, 32);
    // FourCC codec
    ofs.sputn(codec, 4);
    // Width, Height (2 bytes each)
    write_u16_le(ofs, width);
    write_u16_le(ofs, height);
    // Framerate numerator and denominator (4 bytes each)
    write_u32_le(ofs, framerate_num);
    write_u32_le(ofs, framerate_den);
    // Frame count (4 bytes)
    write_u32_le(ofs, frame_count);
    // Unused (4 bytes)
    write_u32_le(ofs, 0);
}

// Write per-frame header (12 bytes): size (4), 64-bit timestamp (we'll use 4 bytes low + 4 bytes high)
static void write_ivf_frame_header(std::stringbuf &ofs, uint32_t frame_size, uint64_t timestamp)
{
    write_u32_le(ofs, frame_size);
    // IVF uses a 64-bit timestamp; write low dword then high dword (little-endian)
    uint32_t ts_low = static_cast<uint32_t>(timestamp & 0xFFFFFFFFu);
    uint32_t ts_high = static_cast<uint32_t>((timestamp >> 32) & 0xFFFFFFFFu);
    write_u32_le(ofs, ts_low);
    write_u32_le(ofs, ts_high);
}

constexpr uint32_t fnv1a_32(const std::string &str)
{
    uint32_t hash = 2166136261u; // offset basis
    constexpr uint32_t prime = 16777619u;

    for (unsigned char c : str) {
        hash ^= c;
        hash *= prime;
    }
    return hash;
}

void setupProcessMonitor(QProcess *ffplay)
{
    QObject::connect(ffplay, &QProcess::stateChanged, [](QProcess::ProcessState newState) {
        qDebug() << "Process State Changed to:" << newState;
    });

    // 2. Catch Critical Errors (e.g., File Not Found, Crashes)
    QObject::connect(ffplay, &QProcess::errorOccurred, [ffplay](QProcess::ProcessError error) {
        qDebug() << "CRITICAL ERROR:" << error;
        qDebug() << "System Message:" << ffplay->errorString();
    });

    // 3. Catch Standard Error Output (Console errors from the app itself)
    QObject::connect(ffplay, &QProcess::readyReadStandardError, [ffplay]() {
        qDebug() << "PROCESS STDERR:" << ffplay->readAllStandardError();
    });

    // 4. Handle Exit
    QObject::connect(ffplay,
                     QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     [](int exitCode, QProcess::ExitStatus exitStatus) {
                         if (exitStatus == QProcess::CrashExit) {
                             qDebug() << "Process crashed with code:" << exitCode;
                         } else {
                             qDebug() << "Process finished normally with code:" << exitCode;
                         }
                     });
}

void ConferenceClient::connectClient(QString url, QString roomId)
{
    rtc::InitLogger(rtc::LogLevel::Debug);

    // rtc::InitLogger(rtc::LogLevel::Debug);
    // When we create a local description (answer), send it to the SFU
    pc.onLocalDescription([this, roomId](rtc::Description desc) {
        const std::string sdp = desc.generateSdp();

        QJsonObject session_desc;
        session_desc["type"] = QStringLiteral("answer");
        session_desc["sdp"] = QString::fromStdString(sdp);

        QJsonObject obj;
        obj["type"] = QStringLiteral("answer");
        obj["roomId"] = roomId;
        obj["data"] = QString::fromStdString(
            QJsonDocument(session_desc).toJson(QJsonDocument::Compact).toStdString());

        const auto json = QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();
        ws.send(json);
    });

    pc.onLocalCandidate([](rtc::Candidate candidate) {
        std::cout << "CANDIDATE" << candidate.candidate() << std::endl;
    });

    // Join room on WebSocket open (protocol-compatible with server)
    ws.onOpen([this, roomId]() {
        QJsonObject obj;
        obj["type"] = QStringLiteral("join");
        obj["data"] = QString();
        obj["sdp"] = QString();
        obj["roomId"] = roomId;

        const auto json = QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();
        ws.send(json);
    });

    // Handle signaling messages from SFU
    ws.onMessage([this](std::variant<binary, string> message) {
        if (!std::holds_alternative<string>(message))
            return;

        const auto text = std::get<string>(message);
        // std::cout << "WebSocket received: " << text << std::endl;

        QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(text));
        if (!doc.isObject())
            return;

        const QJsonObject obj = doc.object();
        const QString typeStr = obj.value("type").toString();
        const QString dataStr = obj.value("data").toString();

        const std::string typeStd = typeStr.toStdString();

        switch (fnv1a_32(typeStd)) {
        case fnv1a_32("candidate"): {
            // data is a JSON-encoded ICECandidateInit
            QJsonDocument iceDoc = QJsonDocument::fromJson(dataStr.toUtf8());
            if (!iceDoc.isObject())
                return;

            const QJsonObject iceObj = iceDoc.object();
            const QString candLine = iceObj.value("candidate").toString();
            if (candLine.isEmpty())
                return;

            rtc::Candidate cand(candLine.toStdString());
            pc.addRemoteCandidate(cand);
            return;
        }
        case fnv1a_32("offer"):
        case fnv1a_32("answer"): {
            std::cout << "got an offer or an answer" << std::endl;
            // For offer/answer, data is a JSON-encoded RTCSessionDescription
            QJsonDocument sdpDoc = QJsonDocument::fromJson(dataStr.toUtf8());
            if (!sdpDoc.isObject())
                return;

            const QJsonObject sdpObj = sdpDoc.object();
            const QString sdp = sdpObj.value("sdp").toString();
            const QString sdpType = sdpObj.value("type").toString();
            if (sdp.isEmpty())
                return;

            rtc::Description::Type descType = rtc::Description::Type::Unspec;
            if (sdpType == QLatin1String("offer"))
                descType = rtc::Description::Type::Offer;
            else if (sdpType == QLatin1String("answer"))
                descType = rtc::Description::Type::Answer;

            rtc::Description desc(sdp.toStdString(), descType);

            if (typeStr == QLatin1String("offer")) {
                // Remote offer from SFU: set as remote description,
                // then ask libdatachannel to create/send an answer
                pc.setRemoteDescription(desc);
                pc.setLocalDescription(rtc::Description::Type::Answer);
            } else {
                // Remote answer from SFU
                pc.setRemoteDescription(desc);
            }

            for (auto track : this->tracks) {
                if (auto ltrack = track.lock()) {
                    ltrack->peek();
                }
            }
            return;
        }
        default:
            return;
        }
    });

    pc.onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
        std::cout << "Gathering State: " << state << std::endl;
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto description = this->pc.localDescription();
            QJsonObject message;
            message["type"] = QString::fromStdString(description->typeString());
            message["sdp"] = QString::fromStdString(description.value());

            // std::cout << QJsonDocument(message).toJson().toStdString() << std::endl;
        }
    });

    this->socket = new QLocalSocket();
    this->socket->connectToServer("video-streams");
    if (!this->socket->waitForConnected()) {
        qDebug() << "Connection failed:" << this->socket->errorString();
        std::abort();
    }

    int *index = new int(0);
    this->config = std::make_shared<rtc::RtpPacketizationConfig>(1,
                                                                 "video-streams",
                                                                 102,
                                                                 rtc::VP8RtpPacketizer::ClockRate);

    this->ofc = new std::fstream();

    ofc->open("dump2.rtp", std::ios_base::out | std::ios_base::trunc);

    pc.onTrack([this, index](std::shared_ptr<rtc::Track> track) {
        std::cout << "track came, type=" << track->description().type() << std::endl;

        // Only handle video tracks
        if (track->description().type() != "video") {
            std::cout << "ignoring non-video track" << std::endl;
            return;
        }

        // Choose depacketizer from SDP codec (first payload type's format)
        std::string codecFormat;
        auto payloadTypes = track->description().payloadTypes();
        if (!payloadTypes.empty()) {
            if (const auto *rtpMap = track->description().rtpMap(payloadTypes[0]))
                codecFormat = rtpMap->format;
        }

        auto mid = track->description().mid();
        if (codecFormat == "VP8") {
            track->setMediaHandler(std::make_shared<rtc::VP8RtpDepacketizer>());
            std::cout << "VP8 codec" << std::endl;
        } else if (codecFormat == "H264") {
            track->setMediaHandler(std::make_shared<rtc::H264RtpDepacketizer>());
            std::cout << "H264 code" << std::endl;
        } else {
            std::cout << "unknown codec '" << codecFormat << "', defaulting to VP8" << std::endl;
            track->setMediaHandler(std::make_shared<rtc::VP8RtpDepacketizer>());
        }
        track->chainMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());

        std::cout << mid << std::endl;

        // Codec FourCC for VP8 is "VP80"
        const char codec[4] = {'V', 'P', '8', '0'};

        if (mid == "2") {
            QMetaObject::invokeMethod(this->socket, [this, index, codec]() {
                // std::stringbuf ofs;

                // write_ivf_file_header(ofs, codec, 720, 720, 140, 1, 1000);

                // this->socket->write(ofs.str().c_str(), ofs.str().size());
                // this->socket->flush();
            });
        }

        track->onFrame([this, mid, index](rtc::binary frame, rtc::FrameInfo info) {
            if (mid == "2") {
                QMetaObject::invokeMethod(this->socket, [this, index, frame]() {
                    rtc::RtpHeader rtp;

                    rtp.setPayloadType(this->config->payloadType);
                    rtp.setSeqNumber((this->config->sequenceNumber)++); // increase sequence number
                    rtp.setTimestamp(this->config->timestamp);
                    rtp.setSsrc(this->config->ssrc);
                    rtp.setMarker(true);

                    rtp.preparePacket();

                    const uint8_t P = 0b0001000;

                    std::stringbuf ofs;
                    // write_ivf_frame_header(ofs, frame.size(), *index);

                    char *buffer = reinterpret_cast<char *>(&rtp);

                    this->socket->write(buffer, sizeof(rtp));
                    this->socket->write(reinterpret_cast<const char *>(&P), 1);
                    this->socket->write(reinterpret_cast<const char *>(frame.data()), frame.size());

                    this->ofc->write(buffer, sizeof(rtp));
                    this->ofc->write(reinterpret_cast<const char *>(&P), 1);
                    this->ofc->write(reinterpret_cast<const char *>(frame.data()), frame.size());
                    this->ofc->flush();

                    // this->socket->write(ofs.str().c_str(), ofs.str().size());
                    this->socket->flush();
                });

                ++(*index);

                this->player->play(mid);
            }

            // QVideoFrame qFrame;

            // this->player->play(qFrame, mid);
        });

        track->onOpen([track]() { track->requestKeyframe(); });

        this->tracks.push_back(track);
    });

    ws.open(url.toStdString());
}
