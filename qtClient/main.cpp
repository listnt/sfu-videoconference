#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QVideoFrame>
#include <QVideoSink>
#include "conferenceclient.h"
#include "videoplayer.h"

void initVideoPlayers(QGuiApplication &app,
                      QQmlApplicationEngine &engine,
                      ConferenceClient *webrtcClient)
{
    QObject *video = engine.rootObjects()[0]->findChild<QObject *>("VideoStreams",
                                                                   Qt::FindChildrenRecursively);

    std::shared_ptr<videoPlayer> player = std::make_shared<videoPlayer>(video, &engine, &app);

    webrtcClient->SetVideoArea(player);
}

void initWebRtcClient(QGuiApplication &app, QQmlApplicationEngine &engine)
{
    ConferenceClient *client = new ConferenceClient();
    engine.rootContext()->setContextProperty("conference_client", client);

    initVideoPlayers(app, engine, client);
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("qtClient", "Main");

    initWebRtcClient(app, engine);

    return app.exec();
}
