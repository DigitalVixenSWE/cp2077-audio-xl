#include "Decode.hpp"

#include <cstring>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "dr_flac.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#pragma warning(push)
#pragma warning(disable : 4244 4245 4456 4457 4701 4702 4996)
#include "stb_vorbis.c"
#pragma warning(pop)

namespace AudioXLNS {

namespace {

void PutU32(std::vector<uint8_t>& aOut, size_t aAt, uint32_t aValue) {
  std::memcpy(aOut.data() + aAt, &aValue, 4);
}

void PutU16(std::vector<uint8_t>& aOut, size_t aAt, uint16_t aValue) {
  std::memcpy(aOut.data() + aAt, &aValue, 2);
}

std::vector<uint8_t> WrapPcm16(const int16_t* aFrames, uint64_t aFrameCount, uint32_t aChannels,
                               uint32_t aRate) {
  const uint64_t dataBytes = aFrameCount * aChannels * 2;
  std::vector<uint8_t> wav(44 + static_cast<size_t>(dataBytes));
  std::memcpy(wav.data(), "RIFF", 4);
  PutU32(wav, 4, static_cast<uint32_t>(wav.size() - 8));
  std::memcpy(wav.data() + 8, "WAVE", 4);
  std::memcpy(wav.data() + 12, "fmt ", 4);
  PutU32(wav, 16, 16);
  PutU16(wav, 20, 1);
  PutU16(wav, 22, static_cast<uint16_t>(aChannels));
  PutU32(wav, 24, aRate);
  PutU32(wav, 28, aRate * aChannels * 2);
  PutU16(wav, 32, static_cast<uint16_t>(aChannels * 2));
  PutU16(wav, 34, 16);
  std::memcpy(wav.data() + 36, "data", 4);
  PutU32(wav, 40, static_cast<uint32_t>(dataBytes));
  std::memcpy(wav.data() + 44, aFrames, static_cast<size_t>(dataBytes));
  return wav;
}

}  

bool DecodeToWav(const std::string& aExt, const std::vector<uint8_t>& aIn, std::vector<uint8_t>& aOut,
                 std::string& aWhy) {
  if (aExt == ".mp3") {
    drmp3_config cfg;
    drmp3_uint64 frames = 0;
    drmp3_int16* pcm = drmp3_open_memory_and_read_pcm_frames_s16(aIn.data(), aIn.size(), &cfg, &frames, nullptr);
    if (!pcm) {
      aWhy = "mp3 decode failed";
      return false;
    }
    aOut = WrapPcm16(pcm, frames, cfg.channels, cfg.sampleRate);
    drmp3_free(pcm, nullptr);
    return true;
  }
  if (aExt == ".flac") {
    unsigned channels = 0;
    unsigned rate = 0;
    drflac_uint64 frames = 0;
    drflac_int16* pcm = drflac_open_memory_and_read_pcm_frames_s16(aIn.data(), aIn.size(), &channels, &rate, &frames, nullptr);
    if (!pcm) {
      aWhy = "flac decode failed";
      return false;
    }
    aOut = WrapPcm16(pcm, frames, channels, rate);
    drflac_free(pcm, nullptr);
    return true;
  }
  if (aExt == ".ogg") {
    int channels = 0;
    int rate = 0;
    short* pcm = nullptr;
    const int frames = stb_vorbis_decode_memory(aIn.data(), static_cast<int>(aIn.size()), &channels, &rate, &pcm);
    if (frames < 0 || !pcm) {
      aWhy = "ogg vorbis decode failed";
      return false;
    }
    aOut = WrapPcm16(pcm, static_cast<uint64_t>(frames), static_cast<uint32_t>(channels), static_cast<uint32_t>(rate));
    free(pcm);
    return true;
  }
  aWhy = "unsupported extension " + aExt;
  return false;
}

}  
