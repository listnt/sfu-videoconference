#include "conferenceclient.h"

typedef int SOCKET;

using rtc::binary;
using rtc::string;

void ConferenceClient::connectClient(QString url, QString roomId)
{
    this->socket = new QLocalSocket();

    rtc::InitLogger(rtc::LogLevel::Debug);

    pc.onLocalDescription(this->pcOnLocalDescription(roomId));
    pc.onLocalCandidate(this->pcOnLocalCandidate());
    pc.onGatheringStateChange(this->pcOnGatheringStateChange());

    ws.onOpen(this->wsOnOpen(roomId));
    ws.onMessage(this->wsOnMessage());

    pc.onTrack(this->pcOnTrack());

    ws.open(url.toStdString());
}

std::function<void(rtc::Description desc)> ConferenceClient::pcOnLocalDescription(QString roomId)
{
    return [this, roomId](rtc::Description desc) {
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
    };
}

std::function<void(rtc::Candidate candidate)> ConferenceClient::pcOnLocalCandidate()
{
    return [](rtc::Candidate candidate) {
        std::cout << "CANDIDATE" << candidate.candidate() << std::endl;
    };
}

std::function<void()> ConferenceClient::wsOnOpen(QString roomId)
{
    return [this, roomId]() {
        QJsonObject obj;
        obj["type"] = QStringLiteral("join");
        obj["data"] = QString();
        obj["sdp"] = QString();
        obj["roomId"] = roomId;

        const auto json = QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();

        ws.send(json);
    };
}

std::function<void(std::variant<rtc::binary, std::string> message)> ConferenceClient::wsOnMessage()
{
    return [this](std::variant<binary, string> message) {
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
                pc.setRemoteDescription(desc);
                pc.setLocalDescription(rtc::Description::Type::Answer);
            } else {
                // Remote answer from SFU
                pc.setRemoteDescription(desc);
            }

            for (const auto &[mid, track_ptr] : this->track_index) {
                if (auto ltrack = track_ptr.track.lock()) {
                    ltrack->peek();
                }
            }
            return;
        }
        default:
            return;
        }
    };
}

std::function<void(rtc::PeerConnection::GatheringState)> ConferenceClient::pcOnGatheringStateChange()
{
    return [this](rtc::PeerConnection::GatheringState state) {
        std::cout << "Gathering State: " << state << std::endl;
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto description = this->pc.localDescription();
            QJsonObject message;
            message["type"] = QString::fromStdString(description->typeString());
            message["sdp"] = QString::fromStdString(description.value());
        }
    };
}

std::function<void(rtc::binary, rtc::FrameInfo)> ConferenceClient::trackOnFrame(std::string mid)
{
    return [this, mid](rtc::binary frame, rtc::FrameInfo info) {
        if (mid == "2") {
            QMetaObject::invokeMethod(this->socket, [this, mid, frame]() {
                std::stringbuf ofs;
                write_ivf_frame_header(ofs, frame.size(), this->track_index[mid].index);

                this->socket->write(ofs.str().c_str(), ofs.str().size());
                this->socket->write(reinterpret_cast<const char *>(frame.data()), frame.size());
                this->socket->flush();
            });

            this->player->play(mid);
        }

        this->track_index[mid].index++;
    };
}

std::function<void(std::shared_ptr<rtc::Track>)> ConferenceClient::pcOnTrack()
{
    return [this](std::shared_ptr<rtc::Track> track) {
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
        this->track_index[mid] = {track, 0};

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
            QMetaObject::invokeMethod(this->socket, [this, codec]() {
                this->socket->setServerName("video-streams");
                this->socket->connectToServer("video-streams");
                if (!this->socket->waitForConnected()) {
                    qDebug() << "Connection failed:" << this->socket->errorString();
                    std::abort();
                }

                std::stringbuf ofs;

                write_ivf_file_header(ofs, codec, 720, 720, 140, 1, 1000);

                this->socket->write(ofs.str().c_str(), ofs.str().size());
                // this->socket->flush();
            });
        }

        track->onFrame(this->trackOnFrame(mid));

        track->onOpen([track]() { track->requestKeyframe(); });
    };
}
