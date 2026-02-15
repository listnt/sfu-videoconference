#include "videoplayer.h"

void videoPlayer::play(std::string mid)
{
    auto output = this->mp[mid];

    if (!output) {
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

            auto voutput = rect->findChild<QObject *>("videoOutput", Qt::FindChildrenRecursively);

            this->mp[mid] = voutput;
            // this->player->setVideoOutput(voutput);

            // this->player->play();

            this->processing[mid] = false;

            std::cout << "Element created" << std::endl;
        });
    }

    // std::cout << this->player->mediaStatus() << std::endl;

    // sink->setVideoFrame(frame);
}
