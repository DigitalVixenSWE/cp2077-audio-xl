#include "AudioFeed.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "SoundRegistry.hpp"

namespace AudioXLNS {

namespace {

struct AkBuffer {
  void* pData;
  uint32_t channelConfig;
  uint32_t eState;
  uint16_t uMaxFrames;
  uint16_t uValidFrames;
};
constexpr uint32_t kNoMoreData = 0x11;   
constexpr uint32_t kDataReady = 0x2D;
constexpr uint32_t kNoDataReady = 46;     

int32_t ReadSample(const uint8_t* p, uint32_t aBytes) {
  if (aBytes == 2) {
    int16_t v;
    std::memcpy(&v, p, 2);
    return v;
  }
  
  return static_cast<int32_t>((static_cast<uint32_t>(p[0]) << 8) | (static_cast<uint32_t>(p[1]) << 16) |
                              (static_cast<uint32_t>(p[2]) << 24)) >> 8;
}

void WriteSample(uint8_t* p, uint32_t aBytes, int32_t aValue) {
  if (aBytes == 2) {
    const int16_t v = static_cast<int16_t>(std::clamp(aValue, -32768, 32767));
    std::memcpy(p, &v, 2);
    return;
  }
  const int32_t v = std::clamp(aValue, -8388608, 8388607);
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
}

}  

AudioFeed* AudioFeed::Get() {
  static AudioFeed instance;
  return &instance;
}

AudioFeed::AudioFeed() : m_voices(kMaxVoices), m_rows(kMaxRows), m_unbound(32), m_binds(kMaxBinds) {}

bool AudioFeed::StillWaiting(uint32_t aPlayingId) {
  Unbound* free = nullptr;
  for (auto& u : m_unbound) {
    if (u.playingId == aPlayingId) {
      if (++u.tries > kMaxUnboundTries) {
        u.playingId = 0;
        u.tries = 0;
        return false;
      }
      return true;
    }
    if (!free && u.playingId == 0) free = &u;
  }
  if (!free) free = &m_unbound[0];
  free->playingId = aPlayingId;
  free->tries = 1;
  return true;
}

AudioFeed::Voice* AudioFeed::Find(uint32_t aPlayingId) {
  for (auto& v : m_voices) {
    if (v.active && v.playingId == aPlayingId) return &v;
  }
  return nullptr;
}

AudioFeed::Voice* AudioFeed::Start(uint32_t aPlayingId, uint16_t aRow, bool aBound) {
  Voice* slot = nullptr;
  for (auto& v : m_voices) {
    if (!v.active) {
      slot = &v;
      break;
    }
  }
  if (!slot) {
    
    auto* reg = SoundRegistry::Get();
    for (auto& v : m_voices) {
      const bool tracked = v.bound ? IsBound(v.playingId) : reg->SlotForPlayingId(v.playingId) != 0xFFFF;
      if (!tracked) {
        Retire(v);
        slot = &v;
        break;
      }
    }
    if (!slot) {
      Retire(m_voices[0]);
      slot = &m_voices[0];
    }
  }
  auto* reg = SoundRegistry::Get();
  const uint32_t total = reg->FramesForRow(aRow);
  const RowFormat* fmt = reg->FormatForRow(aRow);
  const double rateHz = fmt ? static_cast<double>(fmt->sampleRate) : 48000.0;
  *slot = Voice{};
  slot->playingId = aPlayingId;
  slot->row = aRow;
  slot->active = true;
  slot->endFrame = total;
  if (const SoundSpec* spec = reg->SpecForRow(aRow)) {
    slot->loop = spec->loop;
    slot->rate = spec->rate > 0.0f ? spec->rate : 1.0;
    slot->fadeInFrames = spec->fadeIn > 0.0f ? spec->fadeIn * rateHz : 0.0;
    slot->maxFrames = spec->maxDuration > 0.0f ? static_cast<uint64_t>(spec->maxDuration * rateHz) : 0;
    if (spec->start > 0.0f) slot->startFrame = std::min(total, static_cast<uint32_t>(spec->start * rateHz));
    if (spec->end > 0.0f) slot->endFrame = std::min(total, static_cast<uint32_t>(spec->end * rateHz));
    if (slot->endFrame <= slot->startFrame) {
      slot->startFrame = 0;
      slot->endFrame = total;
    }
  }
  slot->pos = slot->startFrame;
  slot->stopGen = m_rows[aRow].stopGen.load(std::memory_order_relaxed);
  slot->bound = aBound;
  if (aBound) {
    if (BoundVoice* b = BindFor(aPlayingId)) slot->bindStopGen = b->stopGen.load(std::memory_order_relaxed);
  }
  m_rows[aRow].live.fetch_add(1, std::memory_order_relaxed);
  return slot;
}

void AudioFeed::Retire(Voice& aVoice) {
  if (aVoice.active && aVoice.row < kMaxRows) {
    auto& live = m_rows[aVoice.row].live;
    uint32_t cur = live.load(std::memory_order_relaxed);
    while (cur > 0 && !live.compare_exchange_weak(cur, cur - 1, std::memory_order_relaxed)) {
    }
  }
  if (aVoice.active && aVoice.bound) Unbind(aVoice.playingId);
  aVoice.active = false;
  aVoice.playingId = 0;
}

AudioFeed::BoundVoice* AudioFeed::BindFor(uint32_t aPlayingId) {
  if (aPlayingId == 0) return nullptr;
  for (auto& b : m_binds) {
    if (b.playingId.load(std::memory_order_acquire) == aPlayingId) return &b;
  }
  return nullptr;
}

bool AudioFeed::Bind(uint32_t aPlayingId, uint16_t aRow) {
  if (aPlayingId == 0 || aRow >= kMaxRows) return false;
  for (auto& b : m_binds) {
    uint32_t expected = 0;
    
    if (b.playingId.load(std::memory_order_relaxed) != 0) continue;
    b.row.store(aRow, std::memory_order_relaxed);
    b.stopFade.store(0.0f, std::memory_order_relaxed);
    if (b.playingId.compare_exchange_strong(expected, aPlayingId, std::memory_order_release)) return true;
  }
  return false;
}

void AudioFeed::Unbind(uint32_t aPlayingId) {
  if (BoundVoice* b = BindFor(aPlayingId)) {
    b->row.store(0xFFFF, std::memory_order_relaxed);
    b->playingId.store(0, std::memory_order_release);
  }
}

uint16_t AudioFeed::BoundRow(uint32_t aPlayingId) const {
  if (aPlayingId == 0) return 0xFFFF;
  for (const auto& b : m_binds) {
    if (b.playingId.load(std::memory_order_acquire) == aPlayingId) return b.row.load(std::memory_order_relaxed);
  }
  return 0xFFFF;
}

void AudioFeed::StopVoice(uint32_t aPlayingId, float aFadeOut) {
  if (BoundVoice* b = BindFor(aPlayingId)) {
    b->stopFade.store(aFadeOut, std::memory_order_relaxed);
    b->stopGen.fetch_add(1, std::memory_order_release);
  }
}

void AudioFeed::Sweep() {
  const uint32_t now = m_sweep.load(std::memory_order_acquire);
  if (now == m_sweepSeen) return;
  m_sweepSeen = now;
  for (auto& v : m_voices) {
    if (v.active && v.bound && !IsBound(v.playingId)) {
      v.bound = false;   
      Retire(v);
    }
  }
}

void AudioFeed::Render(Voice& v, void* aBuffer) {
  auto* buf = static_cast<AkBuffer*>(aBuffer);
  auto* reg = SoundRegistry::Get();
  const RowFormat* fmt = reg->FormatForRow(v.row);
  const uint8_t* pcm = reg->PcmForRow(v.row);
  if (!fmt || !pcm || v.endFrame <= v.startFrame) {
    buf->uValidFrames = 0;
    buf->eState = kNoMoreData;
    Retire(v);
    return;
  }
  const uint32_t channels = fmt->channelConfig & 0xFF;
  const uint32_t bits = fmt->blockAndBits & 0x3F;
  const uint32_t bytesPerSample = bits / 8;
  const uint32_t frameBytes = bytesPerSample * channels;
  if (frameBytes == 0 || (bytesPerSample != 2 && bytesPerSample != 3)) {
    buf->uValidFrames = 0;
    buf->eState = kNoMoreData;
    Retire(v);
    return;
  }
  const double rateHz = fmt->sampleRate;
  auto& ctl = m_rows[v.row];

  const uint32_t gen = ctl.stopGen.load(std::memory_order_acquire);
  if (!v.stopping && v.maxFrames > 0 && v.rendered >= v.maxFrames) {
    v.stopping = true;
    const SoundSpec* spec = reg->SpecForRow(v.row);
    v.stopFadeFrames = spec && spec->fadeOut > 0.0f ? spec->fadeOut * rateHz : 0.0;
    v.stopAtRendered = v.rendered;
  }
  if (!v.stopping && gen != v.stopGen) {
    v.stopping = true;
    v.stopFadeFrames = std::max(0.0, static_cast<double>(ctl.stopFade.load(std::memory_order_relaxed)) * rateHz);
    v.stopAtRendered = v.rendered;
  }
  if (!v.stopping && v.bound) {
    
    if (const BoundVoice* b = BindFor(v.playingId)) {
      if (b->stopGen.load(std::memory_order_acquire) != v.bindStopGen) {
        v.stopping = true;
        v.stopFadeFrames = std::max(0.0, static_cast<double>(b->stopFade.load(std::memory_order_relaxed)) * rateHz);
        v.stopAtRendered = v.rendered;
      }
    }
  }

  const float rowGain = ctl.gain.load(std::memory_order_relaxed);
  const uint32_t maxFrames = buf->uMaxFrames;
  auto* out = static_cast<uint8_t*>(buf->pData);
  uint32_t produced = 0;
  bool ended = false;

  for (uint32_t i = 0; i < maxFrames; ++i) {
    
    if (v.pos >= static_cast<double>(v.endFrame)) {
      if (v.loop && !v.stopping) {
        v.pos = v.startFrame + std::fmod(v.pos - v.endFrame, static_cast<double>(v.endFrame - v.startFrame));
      } else {
        ended = true;
        break;
      }
    }
    
    double env = rowGain;
    if (v.fadeInFrames > 0.0 && static_cast<double>(v.rendered) < v.fadeInFrames) {
      env *= static_cast<double>(v.rendered) / v.fadeInFrames;
    }
    if (v.stopping) {
      const double since = static_cast<double>(v.rendered - v.stopAtRendered);
      if (v.stopFadeFrames <= 0.0 || since >= v.stopFadeFrames) {
        ended = true;
        break;
      }
      env *= 1.0 - since / v.stopFadeFrames;
    }

    const uint32_t f0 = static_cast<uint32_t>(v.pos);
    const bool exact = (v.rate == 1.0) && (env == 1.0);
    const uint8_t* src0 = pcm + static_cast<size_t>(f0) * frameBytes;
    uint8_t* dst = out + static_cast<size_t>(i) * frameBytes;
    if (exact) {
      std::memcpy(dst, src0, frameBytes);
    } else {
      const double frac = v.pos - f0;
      const uint32_t f1 = (f0 + 1 < v.endFrame) ? f0 + 1 : f0;
      const uint8_t* src1 = pcm + static_cast<size_t>(f1) * frameBytes;
      for (uint32_t c = 0; c < channels; ++c) {
        const int32_t a = ReadSample(src0 + c * bytesPerSample, bytesPerSample);
        const int32_t b = ReadSample(src1 + c * bytesPerSample, bytesPerSample);
        const double s = (a + (b - a) * frac) * env;
        WriteSample(dst + c * bytesPerSample, bytesPerSample, static_cast<int32_t>(std::lround(s)));
      }
    }
    v.pos += v.rate;
    ++v.rendered;
    ++produced;
  }

  buf->uValidFrames = static_cast<uint16_t>(produced);
  if (ended && produced < maxFrames) {
    
    std::memset(out + static_cast<size_t>(produced) * frameBytes, 0, static_cast<size_t>(maxFrames - produced) * frameBytes);
    buf->eState = kNoMoreData;
    Retire(v);
  } else {
    buf->eState = kDataReady;
  }
  
  const uint16_t slot = v.bound ? 0xFFFF : reg->SlotForPlayingId(v.playingId);
  if (slot != 0xFFFF) {
    if (uint32_t* p = reg->PositionForSlot(slot)) *p = static_cast<uint32_t>(v.pos);
  }
}

void __fastcall AudioFeed::Execute(uint32_t aPlayingId, void* aBuffer) {
  auto* self = Get();
  auto* buf = static_cast<AkBuffer*>(aBuffer);
  self->Sweep();
  Voice* v = self->Find(aPlayingId);
  if (!v) {
    auto* reg = SoundRegistry::Get();
    uint16_t row = self->BoundRow(aPlayingId);
    const bool bound = row != 0xFFFF;
    if (!bound) {
      const uint16_t slot = reg->SlotForPlayingId(aPlayingId);
      row = slot == 0xFFFF ? 0xFFFF : reg->RowForSlot(slot);
    }
    if (row == 0xFFFF || row >= kMaxRows) {
      if (self->StillWaiting(aPlayingId)) {
        buf->uValidFrames = 0;
        buf->eState = kNoDataReady;
      } else {
        buf->uValidFrames = 0;
        buf->eState = kNoMoreData;
      }
      return;
    }
    for (auto& u : self->m_unbound) {
      if (u.playingId == aPlayingId) {
        u.playingId = 0;
        u.tries = 0;
      }
    }
    v = self->Start(aPlayingId, row, bound);
  }
  self->Render(*v, aBuffer);
}

void __fastcall AudioFeed::Format(uint32_t aPlayingId, void* aFormat) {
  auto* reg = SoundRegistry::Get();
  uint16_t row = Get()->BoundRow(aPlayingId);
  if (row == 0xFFFF) {
    const uint16_t slot = reg->SlotForPlayingId(aPlayingId);
    if (slot == 0xFFFF) return;
    row = reg->RowForSlot(slot);
  }
  if (row == 0xFFFF) return;
  if (const RowFormat* fmt = reg->FormatForRow(row)) {
    std::memcpy(aFormat, fmt, sizeof(RowFormat));
  }
}

void AudioFeed::Stop(uint16_t aRow, float aFadeOut) {
  if (aRow >= kMaxRows) return;
  m_rows[aRow].stopFade.store(aFadeOut, std::memory_order_relaxed);
  m_rows[aRow].stopGen.fetch_add(1, std::memory_order_release);
}

void AudioFeed::SetGain(uint16_t aRow, float aGain) {
  if (aRow >= kMaxRows) return;
  m_rows[aRow].gain.store(std::max(0.0f, aGain), std::memory_order_relaxed);
}

bool AudioFeed::IsPlaying(uint16_t aRow) const {
  if (aRow >= kMaxRows) return false;
  return m_rows[aRow].live.load(std::memory_order_relaxed) > 0;
}

}  
