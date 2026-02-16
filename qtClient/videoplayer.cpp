#include "videoplayer.h"

void videoPlayer::play(std::string mid)
{
    auto output = this->mp[mid];

    if (!output) {
        qint64 pid = this->players[mid]->processId();

        FILE *pipe
            = popen(("wmctrl -lp | grep " + std::to_string(pid) + " | awk '{print $1}'").c_str(),
                    "r");

        char buff[128] = "";
        fgets(buff, 128, pipe);

        std::string winId = std::string(buff);
        if (winId.size() < 4) {
            return;
        }

        winId.pop_back();
        WId windowId = std::stoll(winId, 0, 16);

        std::call_once(this->processed[mid], [this, mid, windowId]() {
            QMetaObject::invokeMethod(this->app, [this, mid, windowId]() {
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

                auto player = QWindow::fromWinId(windowId);
                rectVisual->setProperty("videoWindow", QVariant::fromValue(player));

                auto voutput = rectVisual->findChild<QObject *>("videoOutput",
                                                                Qt::FindChildrenRecursively);

                voutput->blockSignals(true);

                this->mp[mid] = player;

                // this->player->setVideoOutput(voutput);

                // this->player->play();

                std::cout << "Element created, pid:" << this->tmp << std::endl;
            });
        });

        return;
    }

    // std::cout << this->player->mediaStatus() << std::endl;

    // sink->setVideoFrame(frame);
}
