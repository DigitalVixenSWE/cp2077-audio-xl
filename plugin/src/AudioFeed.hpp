#ifndef AUDIOXL_AUDIOFEED_HPP
#define AUDIOXL_AUDIOFEED_HPP

#include <atomic>
#include <cstdint>
#include <vector>

namespace AudioXLNS {

class AudioFeed {
 public:
  static AudioFeed* Get();

  static void __fastcall Execute(uint32_t aPlayingId, void* aBuffer);
  static void __fastcall Format(uint32_t aPlayingId, void* aFormat);

  void Stop(uint16_t aRow, float aFadeOut);
  void SetGain(uint16_t aRow, float aGain);
  bool IsPlaying(uint16_t aRow) const;

  bool Bind(uint32_t aPlayingId, uint16_t aRow);
  void Unbind(uint32_t aPlayingId);
  bool IsBound(uint32_t aPlayingId) const { return BoundRow(aPlayingId) != 0xFFFF; }
  uint16_t BoundRow(uint32_t aPlayingId) const;
  void StopVoice(uint32_t aPlayingId, float aFadeOut);
  
  void RequestSweep() { m_sweep.fetch_add(1, std::memory_order_release); }

 private:
  AudioFeed();

  struct Voice {
    uint32_t playingId = 0;
    uint16_t row = 0xFFFF;
    bool active = false;
    double pos = 0.0;            
    uint64_t rendered = 0;       
    uint32_t stopGen = 0;        
    bool stopping = false;
    double stopFadeFrames = 0.0;
    uint64_t stopAtRendered = 0;
    uint32_t startFrame = 0;
    uint32_t endFrame = 0;       
    bool loop = false;
    double rate = 1.0;
    double fadeInFrames = 0.0;
    uint64_t maxFrames = 0;      
    bool bound = false;          
    uint32_t bindStopGen = 0;    
  };

  struct BoundVoice {
    std::atomic<uint32_t> playingId{0};
    std::atomic<uint16_t> row{0xFFFF};
    std::atomic<uint32_t> stopGen{0};
    std::atomic<float> stopFade{0.0f};
  };

  struct RowControl {
    std::atomic<uint32_t> stopGen{0};
    std::atomic<float> stopFade{0.0f};
    std::atomic<float> gain{1.0f};
    std::atomic<uint32_t> live{0};
  };

  Voice* Find(uint32_t aPlayingId);
  Voice* Start(uint32_t aPlayingId, uint16_t aRow, bool aBound);
  BoundVoice* BindFor(uint32_t aPlayingId);
  void Sweep();
  void Retire(Voice& aVoice);
  void Render(Voice& aVoice, void* aBuffer);

  struct Unbound {
    uint32_t playingId = 0;
    uint32_t tries = 0;
  };
  bool StillWaiting(uint32_t aPlayingId);

  static constexpr size_t kMaxVoices = 256;
  static constexpr size_t kMaxRows = 0x1000;
  static constexpr size_t kMaxBinds = 128;
  static constexpr uint32_t kMaxUnboundTries = 200;
  std::vector<Voice> m_voices;
  std::vector<RowControl> m_rows;
  std::vector<Unbound> m_unbound;
  std::vector<BoundVoice> m_binds;
  std::atomic<uint32_t> m_sweep{0};
  uint32_t m_sweepSeen = 0;
};

}  

#endif  
