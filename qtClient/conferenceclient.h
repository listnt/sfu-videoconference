#ifndef CONFERENCECLIENT_H
#define CONFERENCECLIENT_H
#include <QObject>
#include <QProcess>
#include <QString>
#include "constants.h"
#include "rtc/rtc.hpp"
#include "videoplayer.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <QLocalSocket>

#include <fstream>
#include <functional>
#include <utils.h>

struct track_ptr
{
    QLocalSocket *socket;
    std::weak_ptr<rtc::Track> track;
    int index;
};

class ConferenceClient : public QObject
{
    Q_OBJECT
private:
    rtc::WebSocket ws;
    rtc::PeerConnection pc;
    std::string roomId;

    std::unordered_map<std::string, track_ptr> track_index;
    std::shared_ptr<videoPlayer> player;

public:
    explicit ConferenceClient(QObject *parent = nullptr)
        : QObject(parent) {};
    void SetVideoArea(std::shared_ptr<videoPlayer> player) { this->player = player; };
    Q_INVOKABLE void connectClient(QString url, QString roomId);

private:
    std::function<void(rtc::Description)> pcOnLocalDescription(QString roomId);
    std::function<void(rtc::Candidate candidate)> pcOnLocalCandidate();
    std::function<void()> wsOnOpen(QString roomId);
    std::function<void(std::variant<rtc::binary, std::string> message)> wsOnMessage();
    std::function<void(rtc::PeerConnection::GatheringState)> pcOnGatheringStateChange();
    std::function<void(std::shared_ptr<rtc::Track>)> pcOnTrack();
    std::function<void(rtc::binary, rtc::FrameInfo)> trackOnFrame(std::string mid);
};

#endif // CONFERENCECLIENT_H
