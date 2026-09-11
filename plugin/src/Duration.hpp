// ======================================================================================
// The playing length of an audio file, read from its headers. Nothing is decoded.
//
// VENDORED, essentially unchanged, from Spuddeh's Native Radio Framework
// (github.com/spuddeh/cp2077-native-radio-framework, plugin/src/Duration.hpp), MIT, used with
// his express permission. Original author: Spuddeh. See LICENSE.md.
//
// AudioXL needs it for the same reason he did, from the other end: a length has to be known
// BEFORE the file is decoded, so a long row can be qualified for streaming and its true duration
// reported without ever building a resident PCM image.
//
// An MP3's Xing/Info frame or a walk of its frame headers, a FLAC STREAMINFO block, an Ogg's final
// granule, a WAV's data chunk - the four formats AudioXL loads. Every reader returns 0 for a file
// it cannot measure.
// ======================================================================================

#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace AudioXLNS::duration
{
namespace detail
{

inline uint32_t BE32(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

inline uint32_t LE32(const uint8_t* p)
{
    return (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[0]);
}

inline uint16_t LE16(const uint8_t* p)
{
    return uint16_t((uint16_t(p[1]) << 8) | uint16_t(p[0]));
}

inline uint64_t LE64(const uint8_t* p)
{
    return (uint64_t(LE32(p + 4)) << 32) | uint64_t(LE32(p));
}

// Reads [aFrom, aFrom + aCount) of the file, or as much of it as exists.
inline bool ReadSpan(const std::filesystem::path& aPath, uint64_t aFrom, size_t aCount,
                     std::vector<uint8_t>& aOut)
{
    std::ifstream in(aPath, std::ios::binary);
    if (!in)
    {
        return false;
    }
    in.seekg(static_cast<std::streamoff>(aFrom));
    aOut.assign(aCount, 0);
    in.read(reinterpret_cast<char*>(aOut.data()), static_cast<std::streamsize>(aCount));
    aOut.resize(static_cast<size_t>(in.gcount()));
    return !aOut.empty();
}

inline uint64_t FileSize(const std::filesystem::path& aPath)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(aPath, ec);
    return ec ? 0 : static_cast<uint64_t>(size);
}

// The byte offset past every leading ID3v2 tag. A tag's size is four syncsafe bytes and excludes
// its own ten-byte header and, when flagged, a ten-byte footer.
inline size_t SkipId3v2(const uint8_t* d, size_t n)
{
    size_t at = 0;
    while (at + 10 <= n && d[at] == 'I' && d[at + 1] == 'D' && d[at + 2] == '3')
    {
        size_t size = (size_t(d[at + 6] & 0x7F) << 21) | (size_t(d[at + 7] & 0x7F) << 14) |
                      (size_t(d[at + 8] & 0x7F) << 7) | size_t(d[at + 9] & 0x7F);
        size += 10;
        if (d[at + 5] & 0x10)
        {
            size += 10;
        }
        at += size;
    }
    return at;
}

// --- MP3 -----------------------------------------------------------------------------------------

struct Mp3Frame
{
    size_t length = 0;      // bytes, header included
    uint32_t samples = 0;   // per frame
    uint32_t rate = 0;      // Hz
    size_t sideInfo = 0;    // bytes after the header, before a Xing/Info block
};

inline bool ParseMp3Header(const uint8_t* h, Mp3Frame& f)
{
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0)
    {
        return false;
    }
    const int version = (h[1] >> 3) & 3;  // 0 = MPEG 2.5, 1 = reserved, 2 = MPEG 2, 3 = MPEG 1
    const int layer = (h[1] >> 1) & 3;    // 1 = Layer III, 2 = Layer II, 3 = Layer I
    const int bitrateIdx = h[2] >> 4;
    const int rateIdx = (h[2] >> 2) & 3;
    const int padding = (h[2] >> 1) & 1;
    const int channelMode = h[3] >> 6;  // 3 = mono
    if (version == 1 || layer == 0 || bitrateIdx == 0 || bitrateIdx == 15 || rateIdx == 3)
    {
        return false;
    }

    static const uint16_t kV1[3][16] = {
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0},        // Layer III
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0},       // Layer II
        {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0},    // Layer I
    };
    static const uint16_t kV2[3][16] = {
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},            // Layer III
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},            // Layer II
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0},       // Layer I
    };
    static const uint32_t kRates[4][3] = {
        {11025, 12000, 8000},   // MPEG 2.5
        {0, 0, 0},              // reserved
        {22050, 24000, 16000},  // MPEG 2
        {44100, 48000, 32000},  // MPEG 1
    };

    const bool mpeg1 = version == 3;
    const uint32_t bitrate = 1000u * (mpeg1 ? kV1 : kV2)[layer - 1][bitrateIdx];
    const uint32_t rate = kRates[version][rateIdx];
    if (bitrate == 0 || rate == 0)
    {
        return false;
    }

    f.rate = rate;
    if (layer == 3)
    {
        f.samples = 384;
        f.length = (12 * bitrate / rate + padding) * 4;
    }
    else
    {
        f.samples = (layer == 2 || mpeg1) ? 1152 : 576;
        f.length = (f.samples / 8) * bitrate / rate + padding;
    }
    if (layer == 1)
    {
        f.sideInfo = mpeg1 ? (channelMode == 3 ? 17 : 32) : (channelMode == 3 ? 9 : 17);
    }
    else
    {
        f.sideInfo = 0;
    }
    return f.length > 4;
}

// A frame counts as the first only when the frame after it also parses, so a stray sync byte in a
// tag is never mistaken for the start of the audio.
inline bool FirstMp3Frame(const uint8_t* d, size_t n, size_t& at, Mp3Frame& f)
{
    while (at + 4 <= n)
    {
        if (ParseMp3Header(d + at, f))
        {
            const size_t next = at + f.length;
            Mp3Frame g;
            if (next + 4 > n || ParseMp3Header(d + next, g))
            {
                return true;
            }
        }
        ++at;
    }
    return false;
}

// The frame count written by the encoder, when the first frame carries one. A Xing or Info block
// (the latter is what LAME writes for CBR) sits after the side information; a VBRI block sits at a
// fixed 32 bytes in. Either one means that first frame holds no audio.
inline bool EncoderFrameCount(const uint8_t* d, size_t n, size_t at, const Mp3Frame& f, uint32_t& frames)
{
    const size_t xing = at + 4 + f.sideInfo;
    if (xing + 12 <= n && (std::memcmp(d + xing, "Xing", 4) == 0 || std::memcmp(d + xing, "Info", 4) == 0))
    {
        const uint32_t flags = BE32(d + xing + 4);
        if (flags & 1)
        {
            frames = BE32(d + xing + 8);
            return frames > 0;
        }
        return false;
    }
    const size_t vbri = at + 4 + 32;
    if (vbri + 18 <= n && std::memcmp(d + vbri, "VBRI", 4) == 0)
    {
        frames = BE32(d + vbri + 14);
        return frames > 0;
    }
    return false;
}

inline bool HasEncoderBlock(const uint8_t* d, size_t n, size_t at, const Mp3Frame& f)
{
    const size_t xing = at + 4 + f.sideInfo;
    const size_t vbri = at + 4 + 32;
    return (xing + 4 <= n && (std::memcmp(d + xing, "Xing", 4) == 0 || std::memcmp(d + xing, "Info", 4) == 0)) ||
           (vbri + 4 <= n && std::memcmp(d + vbri, "VBRI", 4) == 0);
}

inline float Mp3Duration(const std::filesystem::path& aPath)
{
    // The head first: the ID3v2 tag, which may carry album art, and the first frame after it.
    std::vector<uint8_t> head;
    if (!ReadSpan(aPath, 0, 64 * 1024, head))
    {
        return 0.0f;
    }
    size_t at = SkipId3v2(head.data(), head.size());
    if (at + 8 * 1024 > head.size())
    {
        // The tag is bigger than the first read. Re-read past it.
        if (!ReadSpan(aPath, 0, at + 64 * 1024, head))
        {
            return 0.0f;
        }
        at = SkipId3v2(head.data(), head.size());
    }

    Mp3Frame first;
    size_t firstAt = at;
    if (FirstMp3Frame(head.data(), head.size(), firstAt, first))
    {
        uint32_t frames = 0;
        if (EncoderFrameCount(head.data(), head.size(), firstAt, first, frames))
        {
            return static_cast<float>(double(frames) * double(first.samples) / double(first.rate));
        }
    }

    // No encoder count, so every frame header is walked. This reads the whole file.
    std::vector<uint8_t> all;
    if (!ReadSpan(aPath, 0, static_cast<size_t>(FileSize(aPath)), all))
    {
        return 0.0f;
    }
    const uint8_t* d = all.data();
    const size_t n = all.size();
    at = SkipId3v2(d, n);

    Mp3Frame f;
    if (!FirstMp3Frame(d, n, at, f))
    {
        return 0.0f;
    }
    const uint32_t rate = f.rate;
    uint64_t samples = 0;
    if (!HasEncoderBlock(d, n, at, f))
    {
        samples += f.samples;
    }
    at += f.length;

    while (at + 4 <= n)
    {
        if (!ParseMp3Header(d + at, f))
        {
            // Junk between frames, or the tag at the end. Resync within a bound, else stop.
            const size_t limit = at + 128 * 1024 < n ? at + 128 * 1024 : n;
            size_t again = at + 1;
            bool found = false;
            while (again + 4 <= limit)
            {
                Mp3Frame g;
                if (ParseMp3Header(d + again, g))
                {
                    const size_t next = again + g.length;
                    Mp3Frame h;
                    if (next + 4 > n || ParseMp3Header(d + next, h))
                    {
                        found = true;
                        break;
                    }
                }
                ++again;
            }
            if (!found)
            {
                break;
            }
            at = again;
            continue;
        }
        samples += f.samples;
        at += f.length;
    }
    return rate ? static_cast<float>(double(samples) / double(rate)) : 0.0f;
}

// --- WAV -----------------------------------------------------------------------------------------

inline float WavDuration(const std::filesystem::path& aPath)
{
    std::ifstream in(aPath, std::ios::binary);
    if (!in)
    {
        return 0.0f;
    }
    uint8_t riff[12];
    if (!in.read(reinterpret_cast<char*>(riff), 12) || std::memcmp(riff, "RIFF", 4) != 0 ||
        std::memcmp(riff + 8, "WAVE", 4) != 0)
    {
        return 0.0f;
    }
    const uint64_t fileSize = FileSize(aPath);
    uint32_t byteRate = 0;
    uint32_t sampleRate = 0;
    uint16_t blockAlign = 0;
    uint8_t chunk[8];
    while (in.read(reinterpret_cast<char*>(chunk), 8))
    {
        const uint32_t size = LE32(chunk + 4);
        const uint64_t body = static_cast<uint64_t>(in.tellg());
        if (std::memcmp(chunk, "fmt ", 4) == 0)
        {
            uint8_t fmt[16];
            if (size < 16 || !in.read(reinterpret_cast<char*>(fmt), 16))
            {
                return 0.0f;
            }
            sampleRate = LE32(fmt + 4);
            byteRate = LE32(fmt + 8);
            blockAlign = LE16(fmt + 12);
        }
        else if (std::memcmp(chunk, "data", 4) == 0)
        {
            uint64_t data = size;
            if (size == 0 || size == 0xFFFFFFFFu || body + size > fileSize)
            {
                data = fileSize > body ? fileSize - body : 0;
            }
            if (byteRate == 0)
            {
                byteRate = sampleRate * blockAlign;
            }
            return byteRate ? static_cast<float>(double(data) / double(byteRate)) : 0.0f;
        }
        in.seekg(static_cast<std::streamoff>(body + size + (size & 1)));
    }
    return 0.0f;
}

// --- FLAC ----------------------------------------------------------------------------------------

inline float FlacDuration(const std::filesystem::path& aPath)
{
    std::vector<uint8_t> head;
    if (!ReadSpan(aPath, 0, 64 * 1024, head))
    {
        return 0.0f;
    }
    size_t at = SkipId3v2(head.data(), head.size());
    if (at + 4 > head.size() || std::memcmp(head.data() + at, "fLaC", 4) != 0)
    {
        return 0.0f;
    }
    at += 4;
    while (at + 4 <= head.size())
    {
        const uint8_t type = head[at] & 0x7F;
        const bool last = (head[at] & 0x80) != 0;
        const size_t size = (size_t(head[at + 1]) << 16) | (size_t(head[at + 2]) << 8) | size_t(head[at + 3]);
        at += 4;
        if (type == 0)
        {
            if (at + 18 > head.size())
            {
                return 0.0f;
            }
            const uint8_t* b = head.data() + at;
            const uint32_t rate = (uint32_t(b[10]) << 12) | (uint32_t(b[11]) << 4) | (uint32_t(b[12]) >> 4);
            const uint64_t total = (uint64_t(b[13] & 0x0F) << 32) | (uint64_t(b[14]) << 24) |
                                   (uint64_t(b[15]) << 16) | (uint64_t(b[16]) << 8) | uint64_t(b[17]);
            return rate ? static_cast<float>(double(total) / double(rate)) : 0.0f;
        }
        if (last)
        {
            break;
        }
        at += size;
    }
    return 0.0f;
}

// --- Ogg Vorbis ----------------------------------------------------------------------------------
// The identification header on the first page gives the rate; the granule position of the last
// page is the total sample count.

inline float OggDuration(const std::filesystem::path& aPath)
{
    std::vector<uint8_t> head;
    if (!ReadSpan(aPath, 0, 64 * 1024, head) || head.size() < 27 || std::memcmp(head.data(), "OggS", 4) != 0)
    {
        return 0.0f;
    }
    const size_t segments = head[26];
    const size_t packet = 27 + segments;
    if (packet + 16 > head.size() || head[packet] != 1 || std::memcmp(head.data() + packet + 1, "vorbis", 6) != 0)
    {
        return 0.0f;
    }
    const uint32_t rate = LE32(head.data() + packet + 12);
    if (rate == 0)
    {
        return 0.0f;
    }

    const uint64_t size = FileSize(aPath);
    const size_t tailLen = static_cast<size_t>(size < 256 * 1024 ? size : 256 * 1024);
    std::vector<uint8_t> tail;
    if (!ReadSpan(aPath, size - tailLen, tailLen, tail) || tail.size() < 27)
    {
        return 0.0f;
    }
    for (size_t i = tail.size() - 27 + 1; i-- > 0;)
    {
        if (std::memcmp(tail.data() + i, "OggS", 4) == 0)
        {
            const uint64_t granule = LE64(tail.data() + i + 6);
            if (granule != ~0ull)
            {
                return static_cast<float>(double(granule) / double(rate));
            }
        }
    }
    return 0.0f;
}

} // namespace detail

// Seconds, or 0 when the file is missing, unsupported, or has no readable length.
inline float AudioDuration(const std::filesystem::path& aPath)
{
    std::string ext = aPath.extension().string();
    for (auto& c : ext)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(aPath, ec))
    {
        return 0.0f;
    }
    if (ext == ".mp3")
    {
        return detail::Mp3Duration(aPath);
    }
    if (ext == ".wav")
    {
        return detail::WavDuration(aPath);
    }
    if (ext == ".flac")
    {
        return detail::FlacDuration(aPath);
    }
    if (ext == ".ogg")
    {
        return detail::OggDuration(aPath);
    }
    return 0.0f;
}

} // namespace AudioXLNS::duration
