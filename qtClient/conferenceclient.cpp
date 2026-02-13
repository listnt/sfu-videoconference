#include "conferenceclient.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <atomic>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>

using rtc::binary;
using rtc::string;

std::string randomId(size_t length)
{
    using std::chrono::high_resolution_clock;
    static thread_local std::mt19937 rng(
        static_cast<unsigned int>(high_resolution_clock::now().time_since_epoch().count()));
    static const std::string characters(
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    std::string id(length, '0');
    std::uniform_int_distribution<int> uniform(0, int(characters.size() - 1));
    std::generate(id.begin(), id.end(), [&]() { return characters.at(uniform(rng)); });
    return id;
}

// Keep FNV-1a hash for fast type switching
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

void ConferenceClient::connect(QString url, QString roomId)
{
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
        std::cout << "WebSocket received: " << text << std::endl;

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

            std::cout << QJsonDocument(message).toJson().toStdString() << std::endl;
        }
    });

    pc.onTrack([this](std::shared_ptr<rtc::Track> track) {
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

        track->onFrame([this, mid](rtc::binary frame, rtc::FrameInfo info) {
            QVideoFrame qFrame;
            this->player->play(qFrame, mid);
        });

        track->onOpen([track]() { track->requestKeyframe(); });

        this->tracks.push_back(track);
    });

    ws.open(url.toStdString());
}
