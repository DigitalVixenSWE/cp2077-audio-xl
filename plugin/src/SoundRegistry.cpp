#include "SoundRegistry.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "AudioFeed.hpp"
#include "AudioXLPlugin.hpp"
#include "Decode.hpp"
#include "Manifest.hpp"

namespace AudioXLNS {

namespace {

constexpr uintptr_t kRvaAudioSystem = 0x34389F0;     
constexpr uintptr_t kRvaTable = 0x3B71070;           
constexpr uintptr_t kRvaCount = 0x48FFE34;           
constexpr uintptr_t kRvaNames = 0x48EE910;           
constexpr uintptr_t kRvaFormats = 0x3A9A790;         
constexpr uintptr_t kRvaPcm = 0x4919E40;             
constexpr uintptr_t kRvaFrames = 0x4915E40;          
constexpr uintptr_t kRvaSlotRow = 0x349CE80;         
constexpr uintptr_t kRvaSlotPlayingId = 0x349DE80;   
constexpr uintptr_t kRvaSlotPos = 0x349FE80;         
constexpr uintptr_t kRvaSlotCount = 0x349AE74;       
constexpr uintptr_t kRvaTableInit = 0xA2D398;        
constexpr uintptr_t kRvaSetCallbacks = 0x1A3EAC0;    
constexpr uintptr_t kRvaRegister = 0x2A3D8F4;        
constexpr uintptr_t kRvaLoadBankMemoryCopy = 0x1AC7E00; 
constexpr uintptr_t kRvaExecuteThunk = 0x2A3AF20;    
constexpr uintptr_t kRvaFormatThunk = 0x2A3AF28;     
constexpr size_t kOffModdedFlag = 0x140;             
constexpr uint32_t kTableCapacity = 0x400;
constexpr uint16_t kMaxRows = 0x1000;

uint32_t ReadU32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

uint16_t ReadU16(const uint8_t* p) {
  uint16_t v;
  std::memcpy(&v, p, 2);
  return v;
}

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

class HeapData : public SoundData {
 public:
  explicit HeapData(std::vector<uint8_t> aBytes) : m_bytes(std::move(aBytes)) {}
  const uint8_t* Data() const override { return m_bytes.data(); }
  size_t Size() const override { return m_bytes.size(); }

 private:
  std::vector<uint8_t> m_bytes;
};

class MappedData : public SoundData {
 public:
  ~MappedData() override {
    if (m_view) UnmapViewOfFile(m_view);
    if (m_mapping) CloseHandle(m_mapping);
    if (m_file != INVALID_HANDLE_VALUE) CloseHandle(m_file);
  }

  static std::shared_ptr<MappedData> Open(const std::string& aPath, bool aPrefault) {
    auto md = std::shared_ptr<MappedData>(new MappedData());
    const std::wstring wpath = std::filesystem::path(aPath).wstring();
    md->m_file = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (md->m_file == INVALID_HANDLE_VALUE) return nullptr;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(md->m_file, &size) || size.QuadPart <= 0 || size.QuadPart > 0x1f400000) return nullptr;
    md->m_size = static_cast<size_t>(size.QuadPart);
    md->m_mapping = CreateFileMappingW(md->m_file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!md->m_mapping) return nullptr;
    md->m_view = MapViewOfFile(md->m_mapping, FILE_MAP_READ, 0, 0, 0);
    if (!md->m_view) return nullptr;
    if (aPrefault) {
      volatile uint8_t sink = 0;
      const auto* p = static_cast<const uint8_t*>(md->m_view);
      for (size_t i = 0; i < md->m_size; i += 4096) sink = p[i];
      (void)sink;
    }
    return md;
  }

  const uint8_t* Data() const override { return static_cast<const uint8_t*>(m_view); }
  size_t Size() const override { return m_size; }

 private:
  MappedData() = default;
  HANDLE m_file = INVALID_HANDLE_VALUE;
  HANDLE m_mapping = nullptr;
  void* m_view = nullptr;
  size_t m_size = 0;
};

}  

SoundRegistry* SoundRegistry::Get() {
  static SoundRegistry instance;
  return &instance;
}

void SoundRegistry::Note(const std::string& aLine) {
  std::lock_guard lock(m_mutex);
  if (m_report.size() < 2000) m_report.push_back(aLine);
}

std::vector<std::string> SoundRegistry::Report() const {
  std::lock_guard lock(m_mutex);
  return m_report;
}

bool SoundRegistry::Init() {
  if (m_ready) return true;
  auto* plugin = AudioXLPlugin::Get();
  const auto* sdk = plugin->Sdk();
  if (!sdk || !sdk->runtime) {
    m_status = "no SDK";
    return false;
  }
  
  const bool is231 = sdk->runtime->major == 2 &&
                     ((sdk->runtime->minor == 3 && sdk->runtime->patch == 1) || sdk->runtime->minor == 31);
  if (!is231) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "unsupported game version %u.%u.%u (built for 2.31)",
                  static_cast<unsigned>(sdk->runtime->major), static_cast<unsigned>(sdk->runtime->minor),
                  static_cast<unsigned>(sdk->runtime->patch));
    m_status = buf;
    return false;
  }
  m_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!m_base) {
    m_status = "game module unresolved";
    return false;
  }
  m_audioSysSlot = reinterpret_cast<void**>(m_base + kRvaAudioSystem);
  m_table = reinterpret_cast<void*>(m_base + kRvaTable);
  m_count = reinterpret_cast<uint16_t*>(m_base + kRvaCount);
  m_names = reinterpret_cast<uint64_t*>(m_base + kRvaNames);
  m_formats = reinterpret_cast<RowFormat*>(m_base + kRvaFormats);
  m_pcm = reinterpret_cast<uint8_t**>(m_base + kRvaPcm);
  m_frames = reinterpret_cast<uint32_t*>(m_base + kRvaFrames);
  m_slotRow = reinterpret_cast<uint16_t*>(m_base + kRvaSlotRow);
  m_slotPlayingId = reinterpret_cast<uint32_t*>(m_base + kRvaSlotPlayingId);
  m_slotPos = reinterpret_cast<uint32_t*>(m_base + kRvaSlotPos);
  m_slotCount = reinterpret_cast<uint16_t*>(m_base + kRvaSlotCount);
  m_tableInit = reinterpret_cast<TableInitFn>(m_base + kRvaTableInit);
  m_setCallbacks = reinterpret_cast<SetCallbacksFn>(m_base + kRvaSetCallbacks);
  m_register = reinterpret_cast<RegisterFn>(m_base + kRvaRegister);
  m_loadBankMemoryCopy = reinterpret_cast<LoadBankMemoryCopyFn>(m_base + kRvaLoadBankMemoryCopy);
  m_executeThunk = reinterpret_cast<void*>(m_base + kRvaExecuteThunk);
  m_formatThunk = reinterpret_cast<void*>(m_base + kRvaFormatThunk);

  wchar_t exe[MAX_PATH];
  if (GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0) {
    
    m_gameRoot = std::filesystem::path(exe).parent_path().parent_path().parent_path().string();
  }
  m_specs.assign(kMaxRows, SoundSpec{});
  m_ready = true;
  m_status = "ready (2.31 addresses)";
  return true;
}

bool SoundRegistry::EnsureEnabled() {
  if (!m_ready) return false;
  if (m_enabled) return true;
  void* audioSys = *m_audioSysSlot;
  if (!audioSys) {
    return false;   
  }
  auto* flag = reinterpret_cast<uint8_t*>(audioSys) + kOffModdedFlag;
  if (*flag != 0) {
    
    Note("engine custom-sound path: enabled by -modded (REDmod rows present)");
  } else {
    *flag = 1;
    m_tableInit(m_table, kTableCapacity);
    Note("engine custom-sound path: enabled by AudioXL (no -modded)");
  }
  
  m_setCallbacks(reinterpret_cast<void*>(&AudioFeed::Execute), reinterpret_cast<void*>(&AudioFeed::Format), nullptr);
  m_enabled = true;
  return true;
}

void SoundRegistry::OnAudioInitDone() {
  if (!EnsureEnabled()) {
    AudioXLPlugin::Get()->Warn("audio init returned but the audio system slot is empty");
    return;
  }
  std::vector<Pending> queue;
  {
    std::lock_guard lock(m_mutex);
    queue.swap(m_queue);
  }
  for (const auto& p : queue) {
    if (!p.bankPath.empty()) {
      LoadBank(p.bankPath, p.source);
    } else {
      Register(p.spec, p.source);
    }
  }
  if (!m_manifestsLoaded) {
    m_manifestsLoaded = true;
    
    const std::string own = "red4ext/plugins/AudioXL/audioxl_routing.bnk";
    if (std::filesystem::exists(ResolvePath(own))) LoadBank(own, "AudioXL");
    Manifest::LoadAll(*this);
  }
}

uint64_t SoundRegistry::Hash(const std::string& aName) const {
  
  return RED4ext::CNamePool::Add(aName.c_str()).hash;
}

bool SoundRegistry::EngineHas(uint64_t aHash) const {
  return RowForName(aHash) != 0xFFFF;
}

uint16_t SoundRegistry::RowForName(uint64_t aHash) const {
  if (!m_ready) return 0xFFFF;
  const uint16_t n = *m_count;
  for (uint16_t i = 0; i < n; ++i) {
    if (m_names[i] == aHash) return i;
  }
  return 0xFFFF;
}

uint16_t SoundRegistry::RowFor(const std::string& aName) const {
  if (!m_ready) return 0xFFFF;
  return RowForName(Hash(aName));
}

float SoundRegistry::Duration(const std::string& aName) const {
  const uint16_t row = RowFor(aName);
  if (row == 0xFFFF) return 0.0f;
  const RowFormat* fmt = FormatForRow(row);
  if (!fmt || fmt->sampleRate == 0) return 0.0f;
  float d = static_cast<float>(FramesForRow(row)) / static_cast<float>(fmt->sampleRate);
  if (const SoundSpec* spec = SpecForRow(row)) {
    if (spec->rate > 0.0f) d /= spec->rate;
    if (spec->end > 0.0f) d = std::min(d, spec->end - spec->start);
  }
  return d;
}

std::string SoundRegistry::Subtitle(const std::string& aName, const std::string& aLocale) const {
  const uint16_t row = RowFor(aName);
  const SoundSpec* spec = row == 0xFFFF ? nullptr : SpecForRow(row);
  if (!spec || spec->subtitles.empty()) return "";
  auto it = spec->subtitles.find(Lower(aLocale));
  if (it != spec->subtitles.end()) return it->second;
  it = spec->subtitles.find("en-us");
  if (it != spec->subtitles.end()) return it->second;
  it = spec->subtitles.find("");
  if (it != spec->subtitles.end()) return it->second;
  return spec->subtitles.begin()->second;
}

std::string SoundRegistry::Speaker(const std::string& aName) const {
  const uint16_t row = RowFor(aName);
  const SoundSpec* spec = row == 0xFFFF ? nullptr : SpecForRow(row);
  return spec ? spec->speaker : "";
}

bool SoundRegistry::Has(const std::string& aName) const {
  if (!m_ready) return false;
  return EngineHas(Hash(aName));
}

int32_t SoundRegistry::Count() const {
  if (!m_ready) return 0;
  return static_cast<int32_t>(*m_count);
}

const SoundSpec* SoundRegistry::SpecForRow(uint16_t aRow) const {
  if (aRow >= m_specs.size() || !m_specs[aRow].valid) return nullptr;
  return &m_specs[aRow];
}

const RowFormat* SoundRegistry::FormatForRow(uint16_t aRow) const { return m_ready ? &m_formats[aRow] : nullptr; }
const uint8_t* SoundRegistry::PcmForRow(uint16_t aRow) const { return m_ready ? m_pcm[aRow] : nullptr; }
uint32_t SoundRegistry::FramesForRow(uint16_t aRow) const { return m_ready ? m_frames[aRow] : 0; }
uint16_t SoundRegistry::RowForSlot(uint16_t aSlot) const { return m_ready ? m_slotRow[aSlot] : 0xFFFF; }
uint32_t* SoundRegistry::PositionForSlot(uint16_t aSlot) const { return m_ready ? &m_slotPos[aSlot] : nullptr; }
uint32_t SoundRegistry::PlayingIdForSlot(uint16_t aSlot) const { return m_ready ? m_slotPlayingId[aSlot] : 0; }
uint16_t SoundRegistry::SlotCount() const { return m_ready ? *m_slotCount : 0; }

uint16_t SoundRegistry::SlotForPlayingId(uint32_t aPlayingId) const {
  if (!m_ready || aPlayingId == 0) return 0xFFFF;
  const uint16_t n = *m_slotCount;
  for (uint16_t i = 0; i < n; ++i) {
    if (m_slotPlayingId[i] == aPlayingId) return i;
  }
  return 0xFFFF;
}

std::string SoundRegistry::ResolvePath(const std::string& aPath) const {
  std::filesystem::path p(aPath);
  if (p.is_absolute() || m_gameRoot.empty()) return p.lexically_normal().string();
  return (std::filesystem::path(m_gameRoot) / p).lexically_normal().string();
}

bool SoundRegistry::ValidateWav(const uint8_t* d, size_t n, std::string& aWhy) const {
  if (n < 0x40 || n > 0x1f400000) {
    aWhy = "size out of range (64 B .. 500 MB)";
    return false;
  }
  if (std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0 ||
      std::memcmp(d + 12, "fmt ", 4) != 0) {
    aWhy = "not a RIFF/WAVE file with a leading fmt chunk";
    return false;
  }
  const uint32_t fmtSize = ReadU32(d + 16);
  if (fmtSize < 16 || 20 + fmtSize >= n) {
    aWhy = "fmt chunk too short";
    return false;
  }
  const uint16_t tag = ReadU16(d + 20);
  const uint16_t channels = ReadU16(d + 22);
  const uint32_t rate = ReadU32(d + 24);
  const uint32_t byteRate = ReadU32(d + 28);
  const uint16_t blockAlign = ReadU16(d + 32);
  const uint16_t bits = ReadU16(d + 34);
  if (tag != 1) {
    aWhy = "format tag must be 1 (plain PCM): no float, no ADPCM, no WAVE_FORMAT_EXTENSIBLE";
    return false;
  }
  if (channels == 0 || channels > 8) {
    aWhy = "channels must be 1..8";
    return false;
  }
  if (rate == 0 || rate > 192000) {
    aWhy = "sample rate must be 1..192000";
    return false;
  }
  if (bits != 16 && bits != 24) {
    aWhy = "bits per sample must be 16 or 24";
    return false;
  }
  const uint32_t frameBits = static_cast<uint32_t>(channels) * bits;
  if (byteRate != (frameBits * rate) / 8 || blockAlign != frameBits / 8) {
    aWhy = "byte rate / block align inconsistent with channels * bits * rate";
    return false;
  }
  size_t pos = 20 + fmtSize;
  while (pos + 8 <= n) {
    const uint32_t chunkSize = ReadU32(d + pos + 4);
    if (std::memcmp(d + pos, "data", 4) == 0) {
      if (pos + 8 + chunkSize > n) {
        aWhy = "data chunk runs past end of file";
        return false;
      }
      return true;
    }
    pos += 8 + chunkSize;
  }
  aWhy = "no data chunk";
  return false;
}

std::shared_ptr<SoundData> SoundRegistry::Load(const SoundSpec& aSpec, std::string& aWhy) {
  const std::string full = ResolvePath(aSpec.path);
  {
    std::lock_guard lock(m_mutex);
    auto it = m_byPath.find(full);
    if (it != m_byPath.end()) return it->second;
  }
  const std::string ext = Lower(std::filesystem::path(full).extension().string());
  std::shared_ptr<SoundData> data;
  if (ext == ".wav") {
    data = MappedData::Open(full, !aSpec.stream);
    if (!data) {
      aWhy = "cannot open " + full;
      return nullptr;
    }
  } else if (ext == ".mp3" || ext == ".ogg" || ext == ".flac") {
    std::ifstream f(full, std::ios::binary | std::ios::ate);
    if (!f) {
      aWhy = "cannot open " + full;
      return nullptr;
    }
    const auto size = f.tellg();
    std::vector<uint8_t> raw(static_cast<size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(raw.data()), size);
    std::vector<uint8_t> wav;
    if (!DecodeToWav(ext, raw, wav, aWhy)) return nullptr;
    data = std::make_shared<HeapData>(std::move(wav));
  } else {
    aWhy = "unsupported file type '" + ext + "' (wav, mp3, ogg, flac)";
    return nullptr;
  }
  std::lock_guard lock(m_mutex);
  m_byPath[full] = data;
  return data;
}

namespace {
const char* AkResultName(int32_t r) {
  switch (r) {
    case 1: return "AK_Success";
    case 2: return "AK_Fail";
    case 7: return "AK_InvalidFile";
    case 8: return "AK_AudioFileHeaderTooLarge";
    case 14: return "AK_InvalidID";
    case 15: return "AK_IDNotFound";
    case 31: return "AK_InvalidParameter";
    case 52: return "AK_InsufficientMemory";
    case 54: return "AK_UnknownBankID";
    case 56: return "AK_BankReadError";
    case 64: return "AK_WrongBankVersion";
    case 66: return "AK_FileNotFound";
    case 69: return "AK_BankAlreadyLoaded";
    case 89: return "AK_DataAlignmentError";
    case 91: return "AK_DuplicateUniqueID";
    case 92: return "AK_InitBankNotLoaded";
    case 100: return "AK_InvalidBankType";
    case 102: return "AK_NotInitialized";
    default: return "AKRESULT?";
  }
}
}  

int32_t SoundRegistry::LoadBank(const std::string& aPath, const std::string& aSource) {
  auto* plugin = AudioXLPlugin::Get();
  if (!m_ready) {
    plugin->Error("LoadBank '" + aPath + "': plugin not available (" + m_status + ")");
    return 102;
  }
  if (!EnsureEnabled()) {
    std::lock_guard lock(m_mutex);
    Pending p;
    p.source = aSource;
    p.bankPath = aPath;
    m_queue.push_back(p);
    return 1;
  }
  const std::string full = ResolvePath(aPath);
  std::ifstream f(full, std::ios::binary | std::ios::ate);
  if (!f) {
    plugin->Error("LoadBank '" + aPath + "' (" + aSource + "): cannot open " + full);
    Note("  BANK FAILED " + aPath + ": cannot open");
    return 66;
  }
  const auto size = f.tellg();
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  f.seekg(0);
  f.read(reinterpret_cast<char*>(bytes.data()), size);
  if (bytes.size() < 8 || std::memcmp(bytes.data(), "BKHD", 4) != 0) {
    plugin->Error("LoadBank '" + aPath + "': not a soundbank (no BKHD)");
    Note("  BANK FAILED " + aPath + ": no BKHD");
    return 7;
  }
  
  std::vector<uint8_t> aligned(bytes.size() + 16);
  auto* p = aligned.data();
  const size_t off = (16 - (reinterpret_cast<uintptr_t>(p) & 15)) & 15;
  std::memcpy(p + off, bytes.data(), bytes.size());
  uint32_t bankId = 0;
  const int32_t r = m_loadBankMemoryCopy(p + off, static_cast<uint32_t>(bytes.size()), &bankId);
  char buf[128];
  std::snprintf(buf, sizeof(buf), "LoadBankMemoryCopy -> %d %s (bank id %08x, %u bytes)", r, AkResultName(r),
                bankId, static_cast<unsigned>(bytes.size()));
  if (r == 1 || r == 69) {
    plugin->Info("LoadBank '" + aPath + "': " + buf);
    Note("  bank " + aPath + ": " + buf);
  } else if (r == 92) {
    
    Note("  bank " + aPath + ": deferred (init bank not loaded yet)");
    std::lock_guard lock(m_mutex);
    Pending p;
    p.source = aSource;
    p.bankPath = aPath;
    m_bankRetry.push_back(p);
  } else {
    plugin->Error("LoadBank '" + aPath + "': " + buf);
    Note("  BANK FAILED " + aPath + ": " + buf);
  }
  return r;
}

int32_t SoundRegistry::RetryBanks() {
  std::vector<Pending> retry;
  {
    std::lock_guard lock(m_mutex);
    retry.swap(m_bankRetry);
  }
  int32_t loaded = 0;
  for (const auto& p : retry) {
    const int32_t r = LoadBank(p.bankPath, p.source);
    if (r == 1 || r == 69) ++loaded;
  }
  return loaded;
}

bool SoundRegistry::Register(const SoundSpec& aSpec, const std::string& aSource) {
  auto* plugin = AudioXLPlugin::Get();
  if (!m_ready) {
    plugin->Error("Register '" + aSpec.name + "': plugin not available (" + m_status + ")");
    return false;
  }
  if (aSpec.name.empty()) {
    plugin->Error("Register from " + aSource + ": a sound has no name");
    return false;
  }
  if (!EnsureEnabled()) {
    std::lock_guard lock(m_mutex);
    m_queue.push_back({aSpec, aSource});
    return true;
  }
  if (aSpec.type == "mod_skip" && aSpec.path.empty()) {
    return Mute(aSpec.name, aSource);
  }
  std::string why;
  auto data = Load(aSpec, why);
  if (!data) {
    plugin->Error("Register '" + aSpec.name + "' (" + aSource + "): " + why);
    Note("  FAILED " + aSpec.name + ": " + why);
    return false;
  }
  return RegisterNow(aSpec, data, aSource);
}

bool SoundRegistry::Mute(const std::string& aName, const std::string& aSource) {
  
  constexpr uint32_t frames = 96;
  std::vector<uint8_t> wav(44 + frames * 2, 0);
  auto put32 = [&](size_t at, uint32_t v) { std::memcpy(wav.data() + at, &v, 4); };
  auto put16 = [&](size_t at, uint16_t v) { std::memcpy(wav.data() + at, &v, 2); };
  std::memcpy(wav.data(), "RIFF", 4);
  put32(4, static_cast<uint32_t>(wav.size() - 8));
  std::memcpy(wav.data() + 8, "WAVE", 4);
  std::memcpy(wav.data() + 12, "fmt ", 4);
  put32(16, 16);
  put16(20, 1);
  put16(22, 1);
  put32(24, 48000);
  put32(28, 48000 * 2);
  put16(32, 2);
  put16(34, 16);
  std::memcpy(wav.data() + 36, "data", 4);
  put32(40, frames * 2);
  if (!m_ready) return false;
  SoundSpec s;
  s.name = aName;
  s.type = "mod_skip";
  if (!EnsureEnabled()) {
    std::lock_guard lock(m_mutex);
    m_queue.push_back({s, aSource});
    return true;
  }
  return RegisterNow(s, std::make_shared<HeapData>(std::move(wav)), aSource);
}

bool SoundRegistry::RegisterNow(const SoundSpec& aSpec, std::shared_ptr<SoundData> aData,
                                const std::string& aSource) {
  auto* plugin = AudioXLPlugin::Get();
  std::string why;
  if (!ValidateWav(aData->Data(), aData->Size(), why)) {
    plugin->Error("Register '" + aSpec.name + "' (" + aSource + "): rejected WAV: " + why);
    Note("  FAILED " + aSpec.name + ": " + why);
    return false;
  }
  
  static const char* const kKnownTypes[] = {"mod_sfx_2d",        "mod_sfx_city",  "mod_sfx_street",
                                            "mod_sfx_room",      "mod_sfx_occlusion",
                                            "mod_sfx_low_occlusion", "mod_sfx_radio", "mod_sfx_ui",
                                            "custom_sound_test", "mod_skip",
                                            
                                            "axl_voice_2d", "axl_music_2d", "axl_sfx_2d", "axl_master_2d",
                                            "axl_radio_2d", "axl_radioport_2d"};
  bool known = false;
  for (const char* t : kKnownTypes) {
    if (aSpec.type == t) known = true;
  }
  if (!known) {
    plugin->Warn("Register '" + aSpec.name + "': type '" + aSpec.type + "' is not a vanilla mod.bnk event; it will only play if a bank defines it");
  }
  std::lock_guard lock(m_mutex);
  const uint64_t hash = Hash(aSpec.name);
  if (EngineHas(hash)) {
    
    plugin->Warn("Register '" + aSpec.name + "' (" + aSource + "): already registered; skipped");
    m_report.push_back("  SKIPPED " + aSpec.name + ": already registered (REDmod or another mod)");
    return false;
  }
  if (*m_count >= kMaxRows) {
    plugin->Error("Register '" + aSpec.name + "': registry full (4096)");
    m_report.push_back("  FAILED " + aSpec.name + ": registry full");
    return false;
  }
  Entry e;
  e.data = const_cast<uint8_t*>(aData->Data());
  e.cap = static_cast<uint32_t>(aData->Size());
  e.size = static_cast<uint32_t>(aData->Size());
  e.file = hash;
  e.type = Hash(aSpec.type);
  e.name = hash;
  e.gain = aSpec.gain;
  e.pitch = aSpec.pitch;
  e.distance = aSpec.distance;
  const uint16_t before = *m_count;
  m_register(&e);
  if (*m_count == before) {
    plugin->Error("Register '" + aSpec.name + "': the engine rolled the row back (WAV parse failed)");
    m_report.push_back("  FAILED " + aSpec.name + ": engine rejected the WAV");
    return false;
  }
  m_buffers.push_back(std::move(aData));
  if (before < m_specs.size()) {
    m_specs[before] = aSpec;
    m_specs[before].valid = true;   
  }
  char buf[96];
  std::snprintf(buf, sizeof(buf), " (row %u, gain %.2f)", static_cast<unsigned>(before), aSpec.gain);
  if (m_report.size() < 2000) m_report.push_back("  " + aSpec.name + " <- " + aSpec.type + buf);
  return true;
}

}  
