#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMediaPlayer>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>

#include "rtc/rtc.hpp"
#include "utils.h"
#include <cstdio>
#include <iostream>
#include <mutex>

#ifdef __linux__
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/mem.h>
}
struct decoder
{
    AVCodec *codec;
    AVCodecParserContext *parser;
    AVCodecContext *c;
    AVFrame *frame;
    AVPacket *pkt;
};

#elif _WIN32
#endif

class videoPlayer
{
private:
    QQmlApplicationEngine *engine;
    QGuiApplication *app;

    QObject *videoArea;

    qint64 tmp;
    std::unordered_map<std::string, QMediaPlayer *> players;
    std::unordered_map<std::string, QVideoSink *> mp;

    std::unordered_map<std::string, QVideoFrame *> qFrames;
    std::unordered_map<std::string, std::once_flag *> frameInit;

#ifdef __linux__
    std::unordered_map<std::string, decoder> frames;
#elif _WIN32
#endif

    std::unordered_map<std::string, std::once_flag *> processed;

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
        if (!videoBlueprint.isError()) {
            std::cout << videoBlueprint.errorString().toStdString() << std::endl;
        }
    };

    void listen(std::string mid)
    {
        this->processed[mid] = new std::once_flag();
        this->frameInit[mid] = new std::once_flag();
        this->mp[mid] = nullptr;
    };
    void play(rtc::binary frame, rtc::FrameInfo info, std::string mid, std::string codec);
    void decode(std::string mid);
};

#endif // VIDEOPLAYER_H
