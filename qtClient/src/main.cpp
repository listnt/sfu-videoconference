#include "conferenceclient.h"
#include "videoplayer.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QVideoFrame>
#include <QVideoSink>

#include <cmath>

void initVideoPlayers(QGuiApplication &app,
                      QQmlApplicationEngine &engine,
                      ConferenceClient *webrtcClient)
{
    QObject *video = engine.rootObjects()[0]->findChild<QObject *>("VideoStreams",
                                                                   Qt::FindChildrenRecursively);

    std::shared_ptr<videoPlayer> player = std::make_shared<videoPlayer>(video, &engine, &app);

    webrtcClient->SetVideoArea(player);
}

void initWebRtcClient(QGuiApplication &app, QQmlApplicationEngine &engine) {
  ConferenceClient *client = new ConferenceClient();
  engine.rootContext()->setContextProperty("conference_client", client);

  initVideoPlayers(app, engine, client);
}

int main(int argc, char *argv[]) {
    std::vector<int> arr = {1, 2, 3, 4, 5};
    std::vector<std::byte> arr2;

    auto *st = reinterpret_cast<std::byte *>(arr.data());
    int size = arr.size() * sizeof(int);

    arr2.insert(arr2.end(), st, st + size);
    for (int i = 0; i < arr2.size(); i++) {
        std::cout << std::to_integer<int>(arr2[i]) << std::endl;
    }

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
