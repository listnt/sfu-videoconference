#include "utils.h"

// Write 32-bit little-endian
void write_u32_le(std::stringbuf &ofs, uint32_t v)
{
    char b[4];
    b[0] = static_cast<char>(v & 0xFF);
    b[1] = static_cast<char>((v >> 8) & 0xFF);
    b[2] = static_cast<char>((v >> 16) & 0xFF);
    b[3] = static_cast<char>((v >> 24) & 0xFF);
    ofs.sputn(b, 4);
}

// Write 16-bit little-endian
void write_u16_le(std::stringbuf &ofs, uint16_t v)
{
    char b[2];
    b[0] = static_cast<char>(v & 0xFF);
    b[1] = static_cast<char>((v >> 8) & 0xFF);
    ofs.sputn(b, 2);
}

// Write IVF file header (32 bytes)
void write_ivf_file_header(std::stringbuf &ofs,
                           const char codec[4],
                           uint16_t width,
                           uint16_t height,
                           uint32_t framerate_num,
                           uint32_t framerate_den,
                           uint32_t frame_count)
{
    // Signature 'DKIF'
    ofs.sputn("DKIF", 4);
    // Version (2 bytes) and header size (2 bytes) -> version 0, header size 32
    write_u16_le(ofs, 0);
    write_u16_le(ofs, 32);
    // FourCC codec
    ofs.sputn(codec, 4);
    // Width, Height (2 bytes each)
    write_u16_le(ofs, width);
    write_u16_le(ofs, height);
    // Framerate numerator and denominator (4 bytes each)
    write_u32_le(ofs, framerate_num);
    write_u32_le(ofs, framerate_den);
    // Frame count (4 bytes)
    write_u32_le(ofs, frame_count);
    // Unused (4 bytes)
    write_u32_le(ofs, 0);
}

// Write per-frame header (12 bytes): size (4), 64-bit timestamp (we'll use 4 bytes low + 4 bytes high)
void write_ivf_frame_header(std::stringbuf &ofs, uint32_t frame_size, uint64_t timestamp)
{
    write_u32_le(ofs, frame_size);
    // IVF uses a 64-bit timestamp; write low dword then high dword (little-endian)
    uint32_t ts_low = static_cast<uint32_t>(timestamp & 0xFFFFFFFFu);
    uint32_t ts_high = static_cast<uint32_t>((timestamp >> 32) & 0xFFFFFFFFu);
    write_u32_le(ofs, ts_low);
    write_u32_le(ofs, ts_high);
}

void setupProcessMonitor(QProcess *ffplay)
{
    QObject::connect(ffplay, &QProcess::stateChanged, [](QProcess::ProcessState newState) {
        qDebug() << "Process State Changed to:" << newState;
    });

    // 2. Catch Critical Errors (e.g., File Not Found, Crashes)
    QObject::connect(ffplay, &QProcess::errorOccurred, [ffplay](QProcess::ProcessError error) {
        qDebug() << "CRITICAL ERROR:" << error;
        qDebug() << "System Message:" << ffplay->errorString();
    });

    // 3. Catch Standard Error Output (Console errors from the app itself)
    QObject::connect(ffplay, &QProcess::readyReadStandardError, [ffplay]() {
        qDebug() << "PROCESS STDERR:" << ffplay->readAllStandardError();
    });

    // 4. Handle Exit
    QObject::connect(ffplay,
                     QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     [](int exitCode, QProcess::ExitStatus exitStatus) {
                         if (exitStatus == QProcess::CrashExit) {
                             qDebug() << "Process crashed with code:" << exitCode;
                         } else {
                             qDebug() << "Process finished normally with code:" << exitCode;
                         }
                     });
}
