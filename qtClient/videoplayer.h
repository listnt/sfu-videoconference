#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QFile>
#include <QGuiApplication>
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
    std::unordered_map<std::string, QVideoSink *> mp;
    std::unordered_map<std::string, bool> processing;
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
    void play(QVideoFrame &frame, std::string mid);
};

#endif // VIDEOPLAYER_H
