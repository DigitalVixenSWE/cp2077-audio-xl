#include "Stream.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "SoundRegistry.hpp"

#define DR_MP3_NO_STDIO
#include "dr_mp3.h"

#define DR_FLAC_NO_STDIO
#include "dr_flac.h"

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#pragma warning(push)
#pragma warning(disable : 4244 4245 4456 4457 4701 4702 4996)
#include "stb_vorbis.c"
#pragma warning(pop)

namespace AudioXLNS {

StreamDecoder::~StreamDecoder() { Close(); }

void StreamDecoder::Close() {
  switch (m_kind) {
    case Kind::Mp3: {
      auto* mp3 = static_cast<drmp3*>(m_handle);
      if (mp3) {
        drmp3_uninit(mp3);
        delete mp3;
      }
      break;
    }
    case Kind::Flac:
      if (m_handle) drflac_close(static_cast<drflac*>(m_handle));
      break;
    case Kind::Vorbis:
      if (m_handle) stb_vorbis_close(static_cast<stb_vorbis*>(m_handle));
      break;
    default:
      break;
  }
  m_handle = nullptr;
  m_kind = Kind::None;
  m_data.reset();
}

bool StreamDecoder::Open(const std::string& aExt, std::shared_ptr<SoundData> aData, std::string& aWhy) {
  Close();
  if (!aData || !aData->Data() || aData->Size() == 0) {
    aWhy = "no data";
    return false;
  }
  
  m_data = std::move(aData);
  const uint8_t* bytes = m_data->Data();
  const size_t size = m_data->Size();

  if (aExt == ".mp3") {
    auto* mp3 = new drmp3();
    if (!drmp3_init_memory(mp3, bytes, size, nullptr)) {
      delete mp3;
      aWhy = "mp3 open failed";
      m_data.reset();
      return false;
    }
    m_handle = mp3;
    m_kind = Kind::Mp3;
    m_rate = mp3->sampleRate;
    m_channels = mp3->channels;
  } else if (aExt == ".flac") {
    drflac* flac = drflac_open_memory(bytes, size, nullptr);
    if (!flac) {
      aWhy = "flac open failed";
      m_data.reset();
      return false;
    }
    m_handle = flac;
    m_kind = Kind::Flac;
    m_rate = flac->sampleRate;
    m_channels = flac->channels;
  } else if (aExt == ".ogg") {
    int err = 0;
    stb_vorbis* vorbis = stb_vorbis_open_memory(bytes, static_cast<int>(size), &err, nullptr);
    if (!vorbis) {
      aWhy = "ogg open failed (" + std::to_string(err) + ")";
      m_data.reset();
      return false;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    m_handle = vorbis;
    m_kind = Kind::Vorbis;
    m_rate = info.sample_rate;
    m_channels = static_cast<uint32_t>(info.channels);
  } else {
    aWhy = "not a streamable type '" + aExt + "'";
    m_data.reset();
    return false;
  }

  if (m_rate == 0 || m_channels == 0) {
    Close();
    aWhy = "decoder reported no format";
    return false;
  }
  return true;
}

uint64_t StreamDecoder::Read(int16_t* aOut, uint64_t aFrames) {
  if (!m_handle || aFrames == 0) return 0;
  switch (m_kind) {
    case Kind::Mp3:
      return drmp3_read_pcm_frames_s16(static_cast<drmp3*>(m_handle), aFrames, aOut);
    case Kind::Flac:
      return drflac_read_pcm_frames_s16(static_cast<drflac*>(m_handle), aFrames, aOut);
    case Kind::Vorbis: {
      
      const int shorts = static_cast<int>(aFrames * m_channels);
      const int got = stb_vorbis_get_samples_short_interleaved(
          static_cast<stb_vorbis*>(m_handle), static_cast<int>(m_channels), aOut, shorts);
      return got > 0 ? static_cast<uint64_t>(got) : 0;
    }
    default:
      return 0;
  }
}

bool StreamDecoder::Seek(uint64_t aFrame) {
  if (!m_handle) return false;
  switch (m_kind) {
    case Kind::Mp3:
      return drmp3_seek_to_pcm_frame(static_cast<drmp3*>(m_handle), aFrame) != 0;
    case Kind::Flac:
      return drflac_seek_to_pcm_frame(static_cast<drflac*>(m_handle), aFrame) != 0;
    case Kind::Vorbis:
      return stb_vorbis_seek(static_cast<stb_vorbis*>(m_handle), static_cast<unsigned int>(aFrame)) != 0;
    default:
      return false;
  }
}

StreamRow::StreamRow(std::string aPath, std::string aExt, uint32_t aRate, uint32_t aChannels,
                     uint64_t aTotalFrames)
    : m_path(std::move(aPath)),
      m_ext(std::move(aExt)),
      m_rate(aRate),
      m_channels(aChannels),
      m_totalFrames(aTotalFrames) {}

StreamRow::~StreamRow() = default;

void StreamRow::RequestSeek(uint64_t aFrame) {
  m_seekTo.store(aFrame, std::memory_order_relaxed);
  m_ended.store(false, std::memory_order_relaxed);
  m_wanted.store(true, std::memory_order_release);
  
  m_seekGen.fetch_add(1, std::memory_order_release);
}

void StreamRow::RequestIdle() { m_wanted.store(false, std::memory_order_release); }

uint64_t StreamRow::Take(int16_t* aOut, uint64_t aFrames) {
  
  if (m_seekDone.load(std::memory_order_acquire) != m_seekGen.load(std::memory_order_acquire)) {
    return 0;
  }
  const uint64_t write = m_write.load(std::memory_order_acquire);
  const uint64_t read = m_read.load(std::memory_order_relaxed);
  const uint64_t have = write > read ? write - read : 0;
  const uint64_t take = std::min(have, aFrames);
  if (take == 0 || m_ring.empty()) return 0;

  const uint64_t at = read % kRingFrames;
  const uint64_t first = std::min(take, kRingFrames - at);
  std::memcpy(aOut, m_ring.data() + at * m_channels,
              static_cast<size_t>(first * m_channels) * sizeof(int16_t));
  if (take > first) {
    std::memcpy(aOut + first * m_channels, m_ring.data(),
                static_cast<size_t>((take - first) * m_channels) * sizeof(int16_t));
  }
  m_read.store(read + take, std::memory_order_release);
  m_readFrame.fetch_add(take, std::memory_order_release);
  return take;
}

bool StreamRow::EnsureOpen() {
  if (m_decoder) return true;
  auto data = MapFile(m_path, false);
  if (!data) return false;
  auto decoder = std::make_unique<StreamDecoder>();
  std::string why;
  if (!decoder->Open(m_ext, std::move(data), why)) return false;
  m_decoder = std::move(decoder);
  if (m_ring.empty()) {
    m_ring.assign(static_cast<size_t>(kRingFrames * m_channels), 0);
  }
  return true;
}

void StreamRow::Release() {
  m_decoder.reset();
  m_write.store(0, std::memory_order_relaxed);
  m_read.store(0, std::memory_order_relaxed);
  m_pumpFrame = 0;
}

bool StreamRow::Exhausted() const {
  if (m_seekDone.load(std::memory_order_acquire) != m_seekGen.load(std::memory_order_acquire)) {
    return false;   
  }
  if (!m_ended.load(std::memory_order_acquire)) return false;
  return m_write.load(std::memory_order_acquire) <= m_read.load(std::memory_order_acquire);
}

bool StreamRow::Service() {
  if (!m_wanted.load(std::memory_order_acquire)) {
    if (m_decoder) Release();
    
    m_seekDone.store(m_seekGen.load(std::memory_order_acquire), std::memory_order_release);
    return false;
  }
  if (!EnsureOpen()) {
    m_seekDone.store(m_seekGen.load(std::memory_order_acquire), std::memory_order_release);
    return false;
  }

  const uint32_t gen = m_seekGen.load(std::memory_order_acquire);
  if (gen != m_seekDone.load(std::memory_order_relaxed)) {
    const uint64_t target = m_seekTo.load(std::memory_order_relaxed);
    m_decoder->Seek(target);
    
    m_write.store(0, std::memory_order_relaxed);
    m_read.store(0, std::memory_order_relaxed);
    m_readFrame.store(target, std::memory_order_relaxed);
    m_pumpFrame = target;
    m_ended.store(false, std::memory_order_relaxed);
  }

  while (!m_ended.load(std::memory_order_relaxed)) {
    const uint64_t write = m_write.load(std::memory_order_relaxed);
    const uint64_t read = m_read.load(std::memory_order_acquire);
    const uint64_t used = write - read;
    if (used >= kRingFrames) break;
    const uint64_t space = kRingFrames - used;
    const uint64_t at = write % kRingFrames;
    const uint64_t run = std::min(space, kRingFrames - at);

    const uint64_t got = m_decoder->Read(m_ring.data() + at * m_channels, run);
    if (got == 0) {
      m_ended.store(true, std::memory_order_relaxed);
      break;
    }
    m_pumpFrame += got;
    m_write.store(write + got, std::memory_order_release);
    if (got < run) break;   
  }

  if (gen != m_seekDone.load(std::memory_order_relaxed)) {
    m_seekDone.store(gen, std::memory_order_release);
  }
  return true;
}

StreamPump* StreamPump::Get() {
  static StreamPump pump;
  return &pump;
}

StreamRow* StreamPump::Add(std::unique_ptr<StreamRow> aRow) {
  std::lock_guard lock(m_mutex);
  m_rows.push_back(std::move(aRow));
  return m_rows.back().get();
}

size_t StreamPump::Count() const {
  std::lock_guard lock(m_mutex);
  return m_rows.size();
}

void StreamPump::Start() {
  if (m_running.exchange(true)) return;
  m_thread = std::thread([this] { Run(); });
}

void StreamPump::Stop() {
  if (!m_running.exchange(false)) return;
  if (m_thread.joinable()) m_thread.join();
}

void StreamPump::Run() {
  while (m_running.load(std::memory_order_acquire)) {
    
    {
      std::lock_guard lock(m_mutex);
      m_live.assign(m_rows.size(), nullptr);
      for (size_t i = 0; i < m_rows.size(); ++i) m_live[i] = m_rows[i].get();
    }
    bool busy = false;
    for (StreamRow* row : m_live) {
      
      busy = row->Service() || busy;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(busy ? 20 : 100));
  }
}

}  
