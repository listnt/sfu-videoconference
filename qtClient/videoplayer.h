#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QFile>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMediaPlayer>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QVideoFrame>
#include <QVideoSink>
#include <iostream>

class videoPlayer
{
private:
    QQmlApplicationEngine *engine;
    QGuiApplication *app;

    QObject *videoArea;

    QMediaPlayer *player;
    std::unordered_map<std::string, QVideoSink *> mp;
    std::unordered_map<std::string, bool> processing;
    int focusVideo = 2;

    QQmlComponent videoBlueprint;
    QLocalServer *server;

public:
    videoPlayer(QObject *videoArea, QQmlApplicationEngine *engine, QGuiApplication *app)
        : videoArea(videoArea)
        , engine(engine)
        , videoBlueprint(engine)
        , app(app)
    {
        videoBlueprint.loadFromModule("qtClient", "SmallVideoRect");
        QLocalServer::removeServer("video-streams");
        this->server = new QLocalServer();

        if (!this->server->listen("video-streams")) {
            qDebug() << "Server failed to start:" << this->server->errorString();
            std::abort();
        }

        this->player = new QMediaPlayer();

        QObject::connect(server, &QLocalServer::newConnection, [&]() {
            QLocalSocket *client = this->server->nextPendingConnection();

            this->player->setSourceDevice(client);

            QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        });
    };
    void play(std::string mid);
};

#endif // VIDEOPLAYER_H
