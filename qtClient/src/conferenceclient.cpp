#include "conferenceclient.h"

typedef int SOCKET;

using rtc::binary;
using rtc::string;

void ConferenceClient::connectClient(QString url, QString roomId)
{
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
        auto mid = track->description().mid();

        this->track_index[mid]
            = {track, 0, "NO_VALUE", 0, 0, LRUCache<std::int32_t, jitterbuffer>(128)};

        std::cout << "track came, type=" << track->description().type();
        std::cout << "\nmid: " << mid << "\nrid: ";
        std::cout << "/nssrc: ";
        for (auto p : track->description().getSSRCs()) {
            std::cout << p << " ";
        }
        std::cout << std::endl;

        std::cout << "attributes:\n";
        for (auto p : track->description().attributes()) {
            std::cout << " " << p << "\n";
        }

        this->player->listen(mid);

        bool isVideo = true;

        if (track->description().type() != "video") {
            isVideo = false;

            track->setMediaHandler(std::make_shared<rtc::OpusRtpDepacketizer>());
            track->chainMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
            track->onFrame(this->trackOnFrame(mid, isVideo));
        } else {
            track->onMessage(this->pcOnMessage(mid));
            // track->setMediaHandler(std::make_shared<rtc::RtcpNackResponder>());
        }

        track->onOpen([track]() { track->requestKeyframe(); });
        track->onClosed([this, mid]() { this->player->destroy(mid); });
    };
}

std::function<void(rtc::binary, rtc::FrameInfo)> ConferenceClient::trackOnFrame(std::string mid,
                                                                                bool isVideo)
{
    return [this, mid, isVideo](rtc::binary frame, rtc::FrameInfo info) {
        auto track = this->track_index[mid].track.lock();
        auto PT = info.payloadType;
        auto codec = track->description().rtpMap(PT)->format;

        this->player->play(frame, mid, codec, isVideo);
    };
}

std::function<void(rtc::message_variant)> ConferenceClient::pcOnMessage(std::string mid)
{
    return [this, mid](rtc::message_variant message) {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        auto nowTs = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

        std::string &codec = this->track_index[mid].codec;
        auto &track_info = this->track_index[mid];
        auto &frame_cache = this->track_index[mid].buff.value();

        try {
            auto msg = std::get<rtc::binary>(message);
            auto rtpHeader = reinterpret_cast<const rtc::RtpHeader *>(msg.data());
            std::uint32_t pkgTs = rtpHeader->timestamp();

            std::vector<std::byte> frame;

            if (!frame_cache.exist(pkgTs)) {
                frame_cache.put(pkgTs, jitterbuffer());

                track_info.ssrc = rtpHeader->ssrc();
                track_info.frame_queue[pkgTs] = std::make_pair(nowTs, std::vector<std::byte>());

                auto track = track_info.track.lock();
                auto PT = rtpHeader->payloadType();

                codec = track->description().rtpMap(PT)->format;
            }

            jitterbuffer &buff = frame_cache.get(pkgTs);

            if (codec == MyApp::VP9CODEC) {
                frame = buff.addVp9Packet(msg, track_info.lastCompletedSeqNum);
            } else if (codec == MyApp::VP8CODEC) {
                frame = buff.addVp8Packet(msg, track_info.lastCompletedSeqNum);
            }

            if (frame.size() > 0) {
                track_info.frame_queue[pkgTs].second = frame;
                track_info.lastCompletedTs = pkgTs;
            }
        } catch (std::exception &e) {
            std::cout << e.what() << std::endl;
            return;
        }

        if (!track_info.frame_queue.empty()) {
            auto it = track_info.frame_queue.begin();
            auto &[rtpTs, dataPair] = *it;
            auto &[creationTs, frame] = dataPair;

            if (!track_info.buff->exist(rtpTs)) {
                track_info.frame_queue.erase(it);
                return;
            }
            auto &jitterbuffer = track_info.buff->get(rtpTs);

            auto requestCounter = 999; // just some arbitrary big number

            if (!frame.empty()) {
                this->player->play(frame, mid, codec, true);
                track_info.frame_queue.erase(it);
            } else if (nowTs - creationTs > NACK_TIMEOUT_MS * (jitterbuffer.nackRequested + 1)) {
                requestCounter = jitterbuffer.nackRequested;
                if (requestCounter > NACK_MAX_TRIES) {
                    track_info.frame_queue.erase(it);
                }

                auto nacks = jitterbuffer.getPacketsToNack();
                if (nacks.size() == 0) {
                    jitterbuffer.nackRequested++;
                    return;
                }

                auto header = rtc::RtcpFbHeader{};
                header.setMediaSourceSSRC(track_info.ssrc);
                header.setPacketSenderSSRC(track_info.ssrc);
                header.header.prepareHeader(205, 1, 2 + uint16_t(nacks.size()));
                const auto *headerPtr = reinterpret_cast<const std::byte *>(&header);

                std::vector<std::byte> msg;

                msg.insert(msg.end(), headerPtr, headerPtr + sizeof(header));
                const auto *dataPtr = reinterpret_cast<const std::byte *>(nacks.data());

                msg.insert(msg.end(), dataPtr, dataPtr + nacks.size() * sizeof(std::uint32_t));

                auto track = track_info.track.lock();
                track->send(msg.data(), msg.size());
                jitterbuffer.nackRequested++;
            }
        }
       
    };
}
