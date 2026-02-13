#ifndef CONFERENCECLIENT_H
#define CONFERENCECLIENT_H
#include <QObject>
#include <QString>
#include "rtc/rtc.hpp"
#include "videoplayer.h"

class ConferenceClient : public QObject
{
    Q_OBJECT
private:
    rtc::WebSocket ws;
    rtc::PeerConnection pc;
    std::string roomId;

    std::vector<std::weak_ptr<rtc::Track>> tracks;
    std::shared_ptr<videoPlayer> player;

public:
    explicit ConferenceClient(QObject *parent = nullptr)
        : QObject(parent) {};
    void setVideoArea(std::shared_ptr<videoPlayer> player) { this->player = player; };
    Q_INVOKABLE void connect(QString url, QString roomId);
};

#endif // CONFERENCECLIENT_H
