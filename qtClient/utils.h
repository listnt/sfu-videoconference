#ifndef UTILS_H
#define UTILS_H

#include <QDebug>
#include <QProcess>

// Write 32-bit little-endian
void write_u32_le(std::stringbuf &ofs, uint32_t v);

// Write 16-bit little-endian
void write_u16_le(std::stringbuf &ofs, uint16_t v);

// Write IVF file header (32 bytes)
void write_ivf_file_header(std::stringbuf &ofs,
                           const char codec[4],
                           uint16_t width,
                           uint16_t height,
                           uint32_t framerate_num,
                           uint32_t framerate_den,
                           uint32_t frame_count);

// Write per-frame header (12 bytes): size (4), 64-bit timestamp (we'll use 4 bytes low + 4 bytes high)
void write_ivf_frame_header(std::stringbuf &ofs, uint32_t frame_size, uint64_t timestamp);

constexpr uint32_t fnv1a_32(const std::string &str)
{
    uint32_t hash = 2166136261u; // offset basis
    constexpr uint32_t prime = 16777619u;

    for (unsigned char c : str) {
        hash ^= c;
        hash *= prime;
    }
    return hash;
}

void setupProcessMonitor(QProcess *ffplay);

#endif // UTILS_H
