#ifndef AUDIOXL_SOUNDREGISTRY_HPP
#define AUDIOXL_SOUNDREGISTRY_HPP

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <RED4ext/RED4ext.hpp>

namespace AudioXLNS {

struct SoundSpec {
  std::string name;
  std::string type = "mod_sfx_2d";
  std::string path;            
  float gain = 1.0f;
  float pitch = 0.0f;
  float distance = 0.0f;
  bool loop = false;
  float fadeIn = 0.0f;         
  float fadeOut = 0.0f;        
  float start = 0.0f;          
  float end = 0.0f;            
  float rate = 1.0f;           
  bool stream = false;         
  std::map<std::string, std::string> subtitles;   
  std::string speaker;         
  bool valid = false;          
};

struct RowFormat {
  uint32_t sampleRate;
  uint32_t channelConfig;      
  uint32_t blockAndBits;       
};

class SoundData {
 public:
  virtual ~SoundData() = default;
  virtual const uint8_t* Data() const = 0;
  virtual size_t Size() const = 0;
};

class SoundRegistry {
 public:
  static SoundRegistry* Get();

  bool Init();
  bool Available() const { return m_ready; }
  const std::string& Status() const { return m_status; }

  bool EnsureEnabled();

  bool Register(const SoundSpec& aSpec, const std::string& aSource);

  bool Mute(const std::string& aName, const std::string& aSource);

  bool Has(const std::string& aName) const;
  int32_t Count() const;
  float Duration(const std::string& aName) const;   
  std::string Subtitle(const std::string& aName, const std::string& aLocale) const;   
  std::string Speaker(const std::string& aName) const;

  int32_t LoadBank(const std::string& aPath, const std::string& aSource);

  int32_t RetryBanks();
  std::vector<std::string> Report() const;

  const SoundSpec* SpecForRow(uint16_t aRow) const;
  const RowFormat* FormatForRow(uint16_t aRow) const;
  const uint8_t* PcmForRow(uint16_t aRow) const;
  uint32_t FramesForRow(uint16_t aRow) const;
  uint16_t RowForName(uint64_t aHash) const;
  uint16_t RowFor(const std::string& aName) const;   
  uint16_t RowForSlot(uint16_t aSlot) const;
  uint32_t* PositionForSlot(uint16_t aSlot) const;
  uint16_t SlotForPlayingId(uint32_t aPlayingId) const;
  uint32_t PlayingIdForSlot(uint16_t aSlot) const;
  uint16_t SlotCount() const;

  void OnAudioInitDone();

  const std::string& GameRoot() const { return m_gameRoot; }
  void Note(const std::string& aLine);

 private:
  SoundRegistry() = default;

  struct Entry {
    uint8_t* data = nullptr;      
    uint32_t cap = 0;             
    uint32_t size = 0;            
    uint8_t path[0x20] = {};      
    uint64_t file = 0;            
    uint64_t type = 0;            
    uint64_t name = 0;            
    float gain = 1.0f;            
    float pitch = 0.0f;           
    float distance = 0.0f;        
    uint32_t pad = 0;
  };
  static_assert(sizeof(Entry) == 0x58, "custom-sound entry layout");

  struct Pending {
    SoundSpec spec;
    std::string source;
    std::string bankPath;   
  };

  bool RegisterNow(const SoundSpec& aSpec, std::shared_ptr<SoundData> aData, const std::string& aSource);
  std::shared_ptr<SoundData> Load(const SoundSpec& aSpec, std::string& aWhy);
  bool ValidateWav(const uint8_t* aBytes, size_t aSize, std::string& aWhy) const;
  std::string ResolvePath(const std::string& aPath) const;
  uint64_t Hash(const std::string& aName) const;
  bool EngineHas(uint64_t aHash) const;

  using TableInitFn = void(__fastcall*)(void* aTable, uint32_t aCapacity);
  using SetCallbacksFn = void(__fastcall*)(void* aExecute, void* aFormat, void* aGain);
  using RegisterFn = void(__fastcall*)(Entry* aEntry);
  using LoadBankMemoryCopyFn = int32_t(__fastcall*)(const void* aData, uint32_t aSize, uint32_t* aOutBankId);

  bool m_ready = false;
  bool m_enabled = false;
  bool m_manifestsLoaded = false;
  std::string m_status;
  uintptr_t m_base = 0;
  void** m_audioSysSlot = nullptr;   
  void* m_table = nullptr;           
  uint16_t* m_count = nullptr;       
  uint64_t* m_names = nullptr;       
  RowFormat* m_formats = nullptr;    
  uint8_t** m_pcm = nullptr;         
  uint32_t* m_frames = nullptr;      
  uint16_t* m_slotRow = nullptr;     
  uint32_t* m_slotPlayingId = nullptr;   
  uint32_t* m_slotPos = nullptr;     
  uint16_t* m_slotCount = nullptr;   
  TableInitFn m_tableInit = nullptr;
  SetCallbacksFn m_setCallbacks = nullptr;
  RegisterFn m_register = nullptr;
  LoadBankMemoryCopyFn m_loadBankMemoryCopy = nullptr;
  void* m_executeThunk = nullptr;
  void* m_formatThunk = nullptr;
  std::string m_gameRoot;

  std::vector<std::shared_ptr<SoundData>> m_buffers;
  std::map<std::string, std::shared_ptr<SoundData>> m_byPath;
  std::vector<SoundSpec> m_specs;            
  std::vector<Pending> m_queue;
  std::vector<Pending> m_bankRetry;
  std::vector<std::string> m_report;
  mutable std::mutex m_mutex;
};

}  

#endif  
