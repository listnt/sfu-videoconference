# sfu-videoconference

A high-performance, WebRTC-based videoconferencing system focused on low-level media handling and custom reliability mechanisms.

# Architecture

* SFU server: written in Go using Pion WebRTC stack
* Native client: build with C++ and Qt/QML for UI
* A fork of [libdatachannel](https://github.com/listnt/libdatachannel): with addition of manual control mechanisms like NACK and JitterBuffer.

# Roadmap
* [✓] Add jitterbuffer for OOO packets
* [✓/x] Add VP9 depaketizer. (implemented only in jitterbuffer)
* [] Add Nack Acknowledgement
* [] Add AV1 depaketizer
* [] Make properly looking native client
* [] Make properly looking web client
* [] Add proper room selection
* [] Add Authorization mechanisms for said rooms
