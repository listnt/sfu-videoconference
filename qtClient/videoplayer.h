#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QFile>
#include <QGuiApplication>
#include <QObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QWindow>

#include "utils.h"
#include <cstdio>
#include <iostream>
#include <latch>
#include <mutex>

class videoPlayer
{
private:
    QQmlApplicationEngine *engine;
    QGuiApplication *app;

    QObject *videoArea;

    qint64 tmp;
    std::unordered_map<std::string, QWindow *> mp;
    std::unordered_map<std::string, QProcess *> players;
    std::unordered_map<std::string, std::once_flag> processed;

    int focusVideo = 2;
    QQmlComponent videoBlueprint;

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
        auto player = new QProcess();
        QStringList arguments;

        std::string pipeName = "/tmp/" + pname;

        std::cout << "launching ffplay, mid: " << mid << std::endl;

        std::remove(pipeName.c_str());
        arguments << "-i" << QString::fromStdString("unix:/" + pipeName) << "-noborder" << "-listen"
                  << "1";

        player->setArguments(arguments);
        player->setProgram("ffplay");
        player->setStandardErrorFile(QProcess::nullDevice());
        player->setStandardOutputFile(QProcess::nullDevice());

        player->start();

        QMetaObject::invokeMethod(this->app, [this, player]() { player->setParent(this->app); });

        std::cout << "ffplay launched, mid: " << mid << std::endl;

        this->players[mid] = player;
    }

    void play(std::string mid);
};

#endif // VIDEOPLAYER_H
