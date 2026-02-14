#include "videoplayer.h"

void videoPlayer::play(std::string mid)
{
    auto sink = this->mp[mid];

    if (!sink) {
        auto proc = this->processing[mid];
        if (proc)
            return;
        this->processing[mid] = true;

        QMetaObject::invokeMethod(this->app, [this, mid]() {
            std::cout << "Creating element" << std::endl;
            QObject *rect = this->videoBlueprint.create();
            if (!this->videoBlueprint.errors().empty()) {
                std::cout << "ERROR" << this->videoBlueprint.errorString().toStdString()
                          << std::endl;
            }

            rect->setParent(this->videoArea);

            QQuickItem *rectVisual = qobject_cast<QQuickItem *>(rect);
            if (!rectVisual) {
                std::abort();
            }
            QQuickItem *videoAreaVisual = qobject_cast<QQuickItem *>(this->videoArea);
            if (!videoAreaVisual) {
                std::abort();
            }

            rectVisual->setParentItem(videoAreaVisual);

            auto output = rect->findChild<QObject *>("videoOutput", Qt::FindChildrenRecursively);

            auto sink = output->property("videoSink").value<QVideoSink *>();

            this->mp[mid] = sink;
            this->processing[mid] = false;

            this->player->setVideoSink(sink);

            std::cout << "Element created" << std::endl;
        });
    }

    // sink->setVideoFrame(frame);
}
