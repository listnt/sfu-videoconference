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
    std::unordered_map<std::string, QObject *> mp;
    std::unordered_map<std::string, bool> processing;
    int focusVideo = 2;

    QQmlComponent videoBlueprint;
    QLocalServer *server;
    bool listening = false;

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

        QObject::connect(this->server, &QLocalServer::newConnection, [&]() {
            QLocalSocket *client = this->server->nextPendingConnection();

            QObject::connect(client, &QLocalSocket::readyRead, [client, this]() {
                if (this->listening) {
                    return;
                }
                this->listening = true;

                std::cout << "starting listening" << std::endl;
                this->player->setSourceDevice(client);
            });

            QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        });

        std::cout << this->server->fullServerName().toStdString() << std::endl;

        this->player = new QMediaPlayer();
    };

    void play(std::string mid);
};

#endif // VIDEOPLAYER_H
