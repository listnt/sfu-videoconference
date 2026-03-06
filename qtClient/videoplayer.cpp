#include "videoplayer.h"

void videoPlayer::play(rtc::binary frame, std::string mid, std::string codec, bool isVideo)
{
    if (!this->mp[mid] && !this->audioSink[mid]) {
        std::call_once(*this->processed[mid], [this, mid, codec, isVideo]() {
            this->frames[mid] = {};

            if (codec == "VP90") {
                std::cout << "ffmpeg player initialized with codec VP9" << std::endl;
                this->frames[mid].codec = avcodec_find_decoder(AV_CODEC_ID_VP9);
            } else if (codec == "VP80") {
                std::cout << "ffmpeg player initialized with codec VP8" << std::endl;
                this->frames[mid].codec = avcodec_find_decoder(AV_CODEC_ID_VP8);
            } else if (codec == "OPUS") {
                this->frames[mid].codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
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
                [this, mid, isVideo]() {
                    std::cout << "Creating element" << std::endl;

                    if (isVideo) {
                        QObject *rect = this->videoBlueprint.create();
                        if (!this->videoBlueprint.errors().empty()) {
                            std::cout << "ERROR" << this->videoBlueprint.errorString().toStdString()
                                      << std::endl;
                        }
                        std::cout << "blueprint created" << std::endl;
                        this->rects[mid] = rect;

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

                        this->players[mid] = new QMediaPlayer();
                        this->mp[mid] = sink;
                        this->players[mid]->setVideoOutput(voutput);
                        this->players[mid]->play();
                    } else {
                        QAudioFormat format;
                        format.setSampleRate(48000);
                        format.setChannelCount(2);
                        format.setSampleFormat(QAudioFormat::Float);

                        QAudioSink *audioSink = new QAudioSink(format);
                        audioSink->setVolume(0.1);
                        this->audioSink[mid] = audioSink;
                        this->audioDevice[mid] = audioSink->start();
                    }

                    std::cout << "Element created" << std::endl;
                },
                Qt::BlockingQueuedConnection);
        });
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
            this->decode(mid, isVideo);
        } else {
            break;
        }
    }
}

void videoPlayer::decode(std::string mid, bool isVideo) {
  auto ret = avcodec_send_packet(this->frames[mid].c, this->frames[mid].pkt);
  if (ret < 0) {
    char errbuf[256];
    av_strerror(ret, errbuf, sizeof(errbuf));
    std::cout << "error occured: " << ret << " " << errbuf << std::endl;
  }
  while (ret >= 0) {
    ret = avcodec_receive_frame(this->frames[mid].c, this->frames[mid].frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      return;

    if (isVideo) {
      QVideoFrameFormat format(QSize(this->frames[mid].frame->width,
                                     this->frames[mid].frame->height),
                               QVideoFrameFormat::PixelFormat::Format_YUV420P);

      QVideoFrame frame(format); // TODO, find a method for frame reuse

      if (frame.map(QVideoFrame::WriteOnly)) {
        for (int i = 0; i < 3; ++i) {
          uint8_t *src = this->frames[mid].frame->data[i];
          uint8_t *dst = frame.bits(i);

          int size =
              this->frames[mid].frame->width * this->frames[mid].frame->height;

          if (frame.bytesPerLine(i) == this->frames[mid].frame->linesize[i]) {
            if (i == 0) {
              memcpy(dst, src, size);
            } else {
              memcpy(dst, src, size >> 2);
            }
          } else { // fuking padding, have to copy line by line
            int dstStride = frame.bytesPerLine(i);
            int srcStride = this->frames[mid].frame->linesize[i];

            int planeHeight = (i == 0) ? frame.height() : frame.height() / 2;
            int bytesToCopy = qMin(dstStride, srcStride);

            for (int y = 0; y < planeHeight; y++) {
              memcpy(dst + (y * dstStride), src + (y * srcStride), bytesToCopy);
            }
          }
        }
        frame.unmap();
      }

      this->mp[mid]->setVideoFrame(frame);
    } else {
      int channels = this->frames[mid].c->channels;
      int samplesPerChannel = this->frames[mid].frame->nb_samples;
      int bytesPerSample = av_get_bytes_per_sample(
          (AVSampleFormat)this->frames[mid].frame->format);

      // std::cout << this->frames[mid].frame->format << std::endl;

      if (!av_sample_fmt_is_planar(
              (AVSampleFormat)this->frames[mid].frame->format)) {
        this->audioDevice[mid]->write(
            (const char *)this->frames[mid].frame->data[0],
            samplesPerChannel * channels * bytesPerSample);
      } else {
        QByteArray buffer;
        buffer.reserve(samplesPerChannel * channels * bytesPerSample);

        for (int i = 0; i < samplesPerChannel; ++i) {
          for (int ch = 0; ch < channels; ++ch) {
            uint8_t *ptr =
                this->frames[mid].frame->data[ch] + (i * bytesPerSample);
            buffer.append((const char *)ptr, bytesPerSample);
          }
        }
        this->audioDevice[mid]->write(buffer);
      }
    }
  }
}

void videoPlayer::destroy(std::string mid) {
  std::cout << "destroying track" << std::endl;
  QMetaObject::invokeMethod(
      this->app,
      [this, mid]() {
        this->mp[mid] = nullptr;

        if (this->players[mid]) {
          this->players[mid]->stop();
          this->players[mid]->deleteLater();
        }

        if (this->rects[mid]) {
          this->rects[mid]->deleteLater();
        }

        if (this->audioSink[mid]) {
          this->audioSink[mid]->deleteLater();
        }
      },
      Qt::BlockingQueuedConnection);
}
