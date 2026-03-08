#include "conferenceclient.h"

typedef int SOCKET;

using rtc::binary;
using rtc::string;

void ConferenceClient::connectClient(QString url, QString roomId) {
  rtc::InitLogger(rtc::LogLevel::Debug);

  pc.onLocalDescription(this->pcOnLocalDescription(roomId));
  pc.onLocalCandidate(this->pcOnLocalCandidate());
  pc.onGatheringStateChange(this->pcOnGatheringStateChange());

  pc.onIceStateChange([](rtc::PeerConnection::IceState state) {
    std::cout << "Ice state changed: " << state << std::endl;
  });
  pc.onStateChange([](rtc::PeerConnection::State state) {
    std::cout << "state changed: " << state << std::endl;
  });

  ws.onOpen(this->wsOnOpen(roomId));
  ws.onMessage(this->wsOnMessage());

  pc.onTrack(this->pcOnTrack());

  ws.open(url.toStdString());
}

std::function<void(rtc::Description desc)>
ConferenceClient::pcOnLocalDescription(QString roomId) {
  return [this, roomId](rtc::Description desc) {
    const std::string sdp = desc.generateSdp();

    QJsonObject session_desc;
    session_desc["type"] = QStringLiteral("answer");
    session_desc["sdp"] = QString::fromStdString(sdp);

    QJsonObject obj;
    obj["type"] = QStringLiteral("answer");
    obj["roomId"] = roomId;
    obj["data"] = QString::fromStdString(QJsonDocument(session_desc)
                                             .toJson(QJsonDocument::Compact)
                                             .toStdString());
    const auto json =
        QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();

    ws.send(json);
  };
}

std::function<void(rtc::Candidate candidate)>
ConferenceClient::pcOnLocalCandidate() {
  return [](rtc::Candidate candidate) {
    std::cout << "CANDIDATE" << candidate.candidate() << std::endl;
  };
}

std::function<void()> ConferenceClient::wsOnOpen(QString roomId) {
  return [this, roomId]() {
    QJsonObject obj;
    obj["type"] = QStringLiteral("join");
    obj["data"] = QString();
    obj["sdp"] = QString();
    obj["roomId"] = roomId;

    const auto json =
        QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();

    ws.send(json);
  };
}

std::function<void(std::variant<rtc::binary, std::string> message)>
ConferenceClient::wsOnMessage() {
  return [this](std::variant<binary, string> message) {
    if (!std::holds_alternative<string>(message))
      return;

    const auto text = std::get<string>(message);
    // std::cout << "WebSocket received: " << text << std::endl;

    QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray::fromStdString(text));
    if (!doc.isObject())
      return;

    const QJsonObject obj = doc.object();
    const QString typeStr = obj.value("type").toString();
    const QString dataStr = obj.value("data").toString();

    const std::string typeStd = typeStr.toStdString();
    const std::regex re("m=.*\\n");

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
        for (int i = 0; i < desc.mediaCount(); i++) {
          try {
            auto m = std::get<rtc::Description::Media *>(desc.media(i));
            if (m->direction() == rtc::Description::Direction::Inactive) {
              if (auto t = this->track_index[m->mid()].track.lock()) {
                t->close();
                this->track_index.erase(m->mid());
              }
            }
          } catch (const std::bad_variant_access &ex) {
            std::cout << ex.what() << '\n';
          }
        }

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

std::function<void(rtc::PeerConnection::GatheringState)>
ConferenceClient::pcOnGatheringStateChange() {
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

std::function<void(std::shared_ptr<rtc::Track>)> ConferenceClient::pcOnTrack() {
    return [this](std::shared_ptr<rtc::Track> track) {
        std::cout << "track came, type=" << track->description().type() << std::endl;

        // Choose depacketizer from SDP codec (first payload type's format)
        std::string codecFormat;
        auto payloadTypes = track->description().payloadTypes();
        if (!payloadTypes.empty()) {
            if (const auto *rtpMap = track->description().rtpMap(payloadTypes[0]))
                codecFormat = rtpMap->format;
        }

        auto mid = track->description().mid();

        this->track_index[mid] = {track, 0, 0, LRUCache<std::int32_t, jitterbuffer>(128)};

        std::cout << "mid: " << mid << "\n rid: " << std::endl;
        std::cout << "ssrc: ";
        for (auto p : track->description().getSSRCs()) {
            std::cout << p << " ";
        }
        std::cout << std::endl;

        std::cout << "attributes: ";
        for (auto p : track->description().attributes()) {
            std::cout << " " << p << "\n";
        }
        std::cout << std::endl;
        this->player->listen(mid);

        std::string codec;
        bool isVideo = true;

        if (track->description().type() != "video") {
            std::cout << "audio accepted" << std::endl;
            codec = "OPUS"; // TODO replace with actual codec selection later
            isVideo = false;

            track->setMediaHandler(std::make_shared<rtc::OpusRtpDepacketizer>());
            track->chainMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
            track->onFrame(this->trackOnFrame(mid, codec, isVideo));
        } else {
            std::cout << "video accepted" << std::endl;
            codec = "VP80"; // TODO replace with actual codec selection later

            track->onMessage(this->pcOnMessage(mid));
            // track->setMediaHandler(std::make_shared<rtc::RtcpNackResponder>());
        }

        track->onOpen([track]() { track->requestKeyframe(); });

        track->onClosed([this, mid]() { this->player->destroy(mid); });
    };
}

std::function<void(rtc::binary, rtc::FrameInfo)>
ConferenceClient::trackOnFrame(std::string mid, std::string codec,
                               bool isVideo) {
    return [this, mid, codec, isVideo](rtc::binary frame, rtc::FrameInfo info) {
        this->player->play(frame, mid, codec, isVideo);
    };
}

std::function<void(rtc::message_variant)> ConferenceClient::pcOnMessage(std::string mid)
{
    return [this, mid](rtc::message_variant message) {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        auto nowTs = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

        try {
            auto msg = std::get<rtc::binary>(message);
            auto rtpHeader = reinterpret_cast<const rtc::RtpHeader *>(msg.data());

            if (!this->track_index[mid].buff.value().exist(rtpHeader->timestamp())) {
                this->track_index[mid].buff.value().put(rtpHeader->timestamp(), jitterbuffer());

                this->track_index[mid].frame_queue[rtpHeader->timestamp()]
                    = std::make_pair(nowTs, std::vector<std::byte>());
            }

            auto frame = this->track_index[mid]
                             .buff.value()
                             .get(rtpHeader->timestamp()) // assertion may fail
                             .addPacket(msg, this->track_index[mid].lastCompletedSeqNum);

            if (frame.size() > 0) {
                this->track_index[mid].frame_queue[rtpHeader->timestamp()].second = frame;
                this->track_index[mid].lastCompletedTs = rtpHeader->timestamp();
            }
        } catch (std::exception &e) {
            std::cout << e.what() << std::endl;
            return;
        }

        if (!this->track_index[mid].frame_queue.empty()) {
            auto it = this->track_index[mid].frame_queue.begin();
            auto &[rtpTs, dataPair] = *it;
            auto &[creationTs, frame] = dataPair;

            if (!frame.empty()) {
                this->player->play(frame, mid, "VP80", true);
                this->track_index[mid].frame_queue.erase(it);
            } else if (nowTs - creationTs > 150) {
                auto savedPackets = this->track_index[mid].buff.value().get(rtpTs);
                this->track_index[mid].frame_queue.erase(it);
            }
        }
    };
}
