#include "videoplayer.h"

void videoPlayer::play(rtc::binary frame, rtc::FrameInfo info, std::string mid, std::string codec)
{
    auto sink = this->mp[mid];
    if (!sink) {
        std::call_once(*this->processed[mid], [this, mid, codec]() {
            this->frames[mid] = {};
            if (codec == "VP90") {
                std::cout << "ffmpeg player initialized with codec VP9" << std::endl;
                this->frames[mid].codec = avcodec_find_decoder(AV_CODEC_ID_VP9);
            } else if (codec == "VP80") {
                std::cout << "ffmpeg player initialized with codec VP8" << std::endl;
                this->frames[mid].codec = avcodec_find_decoder(AV_CODEC_ID_VP8);
            }

            if (!this->frames[mid].codec) {
                std::cout << "codec not found" << codec << std::endl;
                exit(1);
            }

            this->frames[mid].parser = av_parser_init(this->frames[mid].codec->id);
            if (!this->frames[mid].parser) {
                std::cout << "parser not found" << std::endl;
                exit(1);
            }

            this->frames[mid].c = avcodec_alloc_context3(this->frames[mid].codec);
            if (!this->frames[mid].c) {
                std::cout << "contextnot found" << std::endl;
                exit(1);
            }

            if (avcodec_open2(this->frames[mid].c, this->frames[mid].codec, NULL) < 0) {
                std::cout << "failed to open codec" << std::endl;
                exit(1);
            }

            this->frames[mid].frame = av_frame_alloc();
            this->frames[mid].pkt = av_packet_alloc();

            QMetaObject::invokeMethod(
                this->app,
                [this, mid]() {
                    std::cout << "Creating element" << std::endl;
                    QObject *rect = this->videoBlueprint.create();
                    if (!this->videoBlueprint.errors().empty()) {
                        std::cout << "ERROR" << this->videoBlueprint.errorString().toStdString()
                                  << std::endl;
                    }
                    std::cout << "blueprint created" << std::endl;

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

                    auto voutput = rect->findChild<QObject *>("videoOutput",
                                                              Qt::FindChildrenRecursively);

                    auto sink = voutput->property("videoSink").value<QVideoSink *>();

                    this->mp[mid] = sink;
                    this->players[mid] = new QMediaPlayer();
                    this->players[mid]->setVideoOutput(voutput);
                    this->players[mid]->play();

                    std::cout << "Element created" << std::endl;
                },
                Qt::BlockingQueuedConnection);
        });

        return;
    }

    auto start = reinterpret_cast<const uint8_t *>(frame.data());
    auto end = start + frame.size();

    while (start < end) {
        auto ret = av_parser_parse2(this->frames[mid].parser,
                                    this->frames[mid].c,
                                    &(this->frames[mid].pkt->data),
                                    &(this->frames[mid].pkt->size),
                                    start,
                                    frame.size(),
                                    AV_NOPTS_VALUE,
                                    AV_NOPTS_VALUE,
                                    0);
        start += ret;
        if (this->frames[mid].pkt->size) {
            this->decode(mid);
        } else {
            break;
        }
    }
}

void videoPlayer::decode(std::string mid)
{
    auto ret = avcodec_send_packet(this->frames[mid].c, this->frames[mid].pkt);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cout << "error occured: " << errbuf << std::endl;
    }
    while (ret >= 0) {
        ret = avcodec_receive_frame(this->frames[mid].c, this->frames[mid].frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;

        QVideoFrameFormat format(QSize(this->frames[mid].frame->width,
                                       this->frames[mid].frame->height),
                                 QVideoFrameFormat::PixelFormat::Format_YUV420P);

        QVideoFrame frame(format); // TODO, find a method for frame reuse
        // });

        if (frame.map(QVideoFrame::WriteOnly)) {
            for (int i = 0; i < 3; ++i) {
                uint8_t *src = this->frames[mid].frame->data[i];
                uint8_t *dst = frame.bits(i);

                int size = this->frames[mid].frame->width * this->frames[mid].frame->height;

                if (i == 0) {
                    memcpy(dst, src, size);
                } else {
                    memcpy(dst, src, size >> 2);
                }
            }
            frame.unmap();
        }

        this->mp[mid]->setVideoFrame(frame);
    }
}
