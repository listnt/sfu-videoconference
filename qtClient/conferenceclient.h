#ifndef CONFERENCECLIENT_H
#define CONFERENCECLIENT_H
#include <QMediaPlayer>
#include <QNetworkDatagram>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QUdpSocket>
#include "rtc/rtc.hpp"
#include "rtppipe.h"
#include "videoplayer.h"
#include <fstream>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <memory>

#include <QLocalSocket>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

class ConferenceClient : public QObject
{
    Q_OBJECT
private:
    rtc::WebSocket ws;
    rtc::PeerConnection pc;
    std::string roomId;

    std::vector<std::weak_ptr<rtc::Track>> tracks;
    std::shared_ptr<videoPlayer> player;

    rtc::shared_ptr<rtc::RtpPacketizationConfig> config;

    QLocalSocket *socket;

    std::fstream *ofc;

public:
    explicit ConferenceClient(QObject *parent = nullptr)
        : QObject(parent) {};
    void setVideoArea(std::shared_ptr<videoPlayer> player) { this->player = player; };
    Q_INVOKABLE void connectClient(QString url, QString roomId);
};

#endif // CONFERENCECLIENT_H
