#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QFile>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMediaPlayer>
#include <QObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QVideoFrame>
#include <QVideoSink>
#include "utils.h"
#include <iostream>

#include <sys/stat.h>

class videoPlayer
{
private:
    QQmlApplicationEngine *engine;
    QGuiApplication *app;

    QObject *videoArea;

    std::vector<std::shared_ptr<QProcess>> players;
    std::unordered_map<std::string, QObject *> mp;
    std::unordered_map<std::string, bool> processing;
    int focusVideo = 2;

    QQmlComponent videoBlueprint;
    bool listening = false;

public:
    videoPlayer(QObject *videoArea, QQmlApplicationEngine *engine, QGuiApplication *app)
        : videoArea(videoArea)
        , engine(engine)
        , videoBlueprint(engine)
        , app(app)
    {
        videoBlueprint.loadFromModule("qtClient", "SmallVideoRect");
    };

    void listen(std::string pname, std::string mid)
    {
        auto player = std::make_shared<QProcess>();
        QStringList arguments;

        std::string pipeName = "/tmp/" + pname;

        unlink(pipeName.c_str());
        arguments << "-i" << QString::fromStdString("unix:/" + pipeName) << "-listen" << "1";

        player->setArguments(arguments);
        player->setProgram("ffplay");
        player->setStandardErrorFile(QProcess::nullDevice());
        player->setStandardOutputFile(QProcess::nullDevice());

        player->start();

        players.push_back(player);
    }

    void play(std::string mid);
};

#endif // VIDEOPLAYER_H
