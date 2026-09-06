#include "Emitters.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <RED4ext/RED4ext.hpp>

#include "AudioFeed.hpp"
#include "AudioXLPlugin.hpp"
#include "SafeReloc.hpp"
#include "SoundRegistry.hpp"

namespace AudioXLNS {

namespace {

constexpr uint32_t kHashRegisterGameObj = 4284289497;    
constexpr uint32_t kHashUnregisterGameObj = 790631100;   
constexpr uint32_t kHashSetPosition = 4207091891;        
constexpr uint32_t kHashSetAuxSends = 445062521;         
constexpr uint32_t kHashPostEvent = 2586650354;          
constexpr uintptr_t kRvaRegisterGameObj = 0x1ACBAB0;
constexpr uintptr_t kRvaUnregisterGameObj = 0x1AD1930;
constexpr uintptr_t kRvaSetPosition = 0x1ACF370;
constexpr uintptr_t kRvaSetAuxSends = 0x1ACE8B0;
constexpr uintptr_t kRvaPostEvent = 0x1AC8D90;

struct AkWorldTransform {
  float front[3];
  float top[3];
  double position[3];
};
static_assert(sizeof(AkWorldTransform) == 48, "AkWorldTransform layout");

struct AkAuxSendValue {
  uint64_t listenerID;
  uint32_t auxBusID;
  float fControlValue;
};

constexpr uint64_t kAllListeners = ~0ull;         
constexpr uint32_t kSetPositionDefault = 3;      
constexpr uint64_t kObjectBase = 0xA5D1000000000000ull;   

using RegisterGameObjFn = uint32_t(__fastcall*)(uint64_t);
using UnregisterGameObjFn = uint32_t(__fastcall*)(uint64_t);
using SetPositionFn = uint32_t(__fastcall*)(uint64_t, const AkWorldTransform*, uint32_t);
using SetAuxSendsFn = uint32_t(__fastcall*)(uint64_t, AkAuxSendValue*, uint32_t);
using PostEventFn = uint32_t(__fastcall*)(uint32_t, uint64_t, uint32_t, void*, void*, uint32_t, void*, uint32_t);

RegisterGameObjFn s_registerGameObj = nullptr;
UnregisterGameObjFn s_unregisterGameObj = nullptr;
SetPositionFn s_setPosition = nullptr;
SetAuxSendsFn s_setAuxSends = nullptr;
PostEventFn s_postEvent = nullptr;

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

}  

Emitters* Emitters::Get() {
  static Emitters instance;
  return &instance;
}

bool Emitters::Init() {
  if (m_ready) return true;
  auto* plugin = AudioXLPlugin::Get();
  const auto* sdk = plugin->Sdk();
  if (!sdk || !sdk->runtime) {
    m_status = "no SDK";
    return false;
  }
  const bool known231 = sdk->runtime->major == 2 &&
                        ((sdk->runtime->minor == 3 && sdk->runtime->patch == 1) || sdk->runtime->minor == 31);
  const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  std::string missing;
  std::string collisions;
  const auto resolve = [&](uint32_t aHash, const char* aName, uintptr_t aRva) -> uintptr_t {
    const uintptr_t byHash = DV::ResolveAddress(aHash);
    if (known231 && moduleBase) {
      const uintptr_t byRva = moduleBase + aRva;
      if (byHash && byHash != byRva) {
        if (!collisions.empty()) collisions += ", ";
        collisions += aName;
      }
      return byRva;
    }
    if (!byHash) {
      if (!missing.empty()) missing += ", ";
      missing += aName;
    }
    return byHash;
  };
  s_registerGameObj = reinterpret_cast<RegisterGameObjFn>(resolve(kHashRegisterGameObj, "EmitterRegister", kRvaRegisterGameObj));
  s_unregisterGameObj = reinterpret_cast<UnregisterGameObjFn>(resolve(kHashUnregisterGameObj, "EmitterUnregister", kRvaUnregisterGameObj));
  s_setPosition = reinterpret_cast<SetPositionFn>(resolve(kHashSetPosition, "EmitterPosition", kRvaSetPosition));
  s_setAuxSends = reinterpret_cast<SetAuxSendsFn>(resolve(kHashSetAuxSends, "EmitterReverb", kRvaSetAuxSends));
  s_postEvent = reinterpret_cast<PostEventFn>(resolve(kHashPostEvent, "EmitterPost", kRvaPostEvent));
  if (!missing.empty()) {
    m_status = "emitters unavailable, address hashes unresolved: " + missing;
    plugin->Warn(m_status);
    return false;
  }
  if (!collisions.empty()) {
    plugin->Warn("emitter address hash collides with another symbol (using the known 2.31 RVA): " + collisions);
  }
  m_nextObject = kObjectBase + 1;
  m_ready = true;
  m_status = "ready";
  return true;
}

Emitters::Emitter* Emitters::Find(const std::string& aName) {
  auto it = m_emitters.find(Lower(aName));
  return it == m_emitters.end() ? nullptr : &it->second;
}

bool Emitters::SetPosition(Emitter& e) {
  AkWorldTransform t{};
  t.front[0] = 0.0f;
  t.front[1] = 0.0f;
  t.front[2] = 1.0f;   
  t.top[0] = 0.0f;
  t.top[1] = 1.0f;     
  t.top[2] = 0.0f;
  t.position[0] = e.pos[0];
  t.position[1] = e.pos[2];
  t.position[2] = e.pos[1];
  return s_setPosition(e.object, &t, kSetPositionDefault) == 1;
}

void Emitters::ApplyReverb(Emitter& e, const std::string& aBus, float aLevel) {
  const uint32_t bus = aBus.empty() ? 0u : WwiseHash(aBus);
  const float level = std::clamp(aLevel, 0.0f, 1.0f);
  if (bus == e.appliedBus && std::fabs(level - e.appliedLevel) < 0.0005f) return;
  if (bus == 0 || level <= 0.0f) {
    s_setAuxSends(e.object, nullptr, 0);
    e.appliedBus = 0;
    e.appliedLevel = 0.0f;
    return;
  }
  AkAuxSendValue v{kAllListeners, bus, level};
  const uint32_t r = s_setAuxSends(e.object, &v, 1);
  e.appliedBus = bus;
  e.appliedLevel = level;
  if (r != 1) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "emitter '%s': reverb send to '%s' refused (AKRESULT %u)", e.name.c_str(),
                  aBus.c_str(), r);
    AudioXLPlugin::Get()->Warn(buf);
  }
}

void Emitters::Prune(Emitter& e) {
  auto* feed = AudioFeed::Get();
  e.voices.erase(std::remove_if(e.voices.begin(), e.voices.end(),
                                [feed](uint32_t pid) { return !feed->IsBound(pid); }),
                 e.voices.end());
}

bool Emitters::Create(const std::string& aName, float aX, float aY, float aZ) {
  if (!Init() || aName.empty()) return false;
  std::lock_guard lock(m_mutex);
  if (Emitter* e = Find(aName)) {
    
    e->pos[0] = aX;
    e->pos[1] = aY;
    e->pos[2] = aZ;
    return SetPosition(*e);
  }
  Emitter e;
  e.name = aName;
  e.object = m_nextObject++;
  e.pos[0] = aX;
  e.pos[1] = aY;
  e.pos[2] = aZ;
  const uint32_t r = s_registerGameObj(e.object);
  if (r != 1) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "emitter '%s': the audio engine refused a new emitter object (%u)", aName.c_str(), r);
    AudioXLPlugin::Get()->Warn(buf);
    return false;
  }
  SetPosition(e);
  auto& stored = m_emitters[Lower(aName)];
  stored = std::move(e);
  char buf[160];
  std::snprintf(buf, sizeof(buf), "  emitter '%s' at (%.1f, %.1f, %.1f)", aName.c_str(), aX, aY, aZ);
  SoundRegistry::Get()->Note(buf);
  return true;
}

bool Emitters::Move(const std::string& aName, float aX, float aY, float aZ) {
  if (!m_ready) return false;
  std::lock_guard lock(m_mutex);
  Emitter* e = Find(aName);
  if (!e) return false;
  e->pos[0] = aX;
  e->pos[1] = aY;
  e->pos[2] = aZ;
  return SetPosition(*e);
}

bool Emitters::SetReverb(const std::string& aName, const std::string& aBus, float aLevel) {
  if (!m_ready) return false;
  std::lock_guard lock(m_mutex);
  Emitter* e = Find(aName);
  if (!e) return false;
  e->reverbBus = (aBus == "None") ? std::string() : aBus;
  e->reverbLevel = aLevel;
  ApplyReverb(*e, e->reverbBus, e->reverbLevel);
  char buf[200];
  std::snprintf(buf, sizeof(buf), "  emitter '%s' reverb %s %.2f (bus id %u)", aName.c_str(),
                e->reverbBus.empty() ? "off" : e->reverbBus.c_str(), aLevel, e->appliedBus);
  SoundRegistry::Get()->Note(buf);
  return true;
}

bool Emitters::Play(const std::string& aName, const std::string& aRow) {
  if (!m_ready) return false;
  auto* reg = SoundRegistry::Get();
  auto* plugin = AudioXLPlugin::Get();
  const uint16_t row = reg->RowFor(aRow);
  if (row == 0xFFFF) {
    plugin->Warn("emitter '" + aName + "': no registered sound named '" + aRow + "'");
    return false;
  }
  std::lock_guard lock(m_mutex);
  Emitter* e = Find(aName);
  if (!e) {
    plugin->Warn("emitter '" + aName + "' does not exist; create it first");
    return false;
  }
  const SoundSpec* spec = reg->SpecForRow(row);
  
  const std::string type = spec ? spec->type : std::string("mod_sfx_2d");
  if (type == "mod_skip") return false;
  
  if (!e->reverbBus.empty()) {
    ApplyReverb(*e, e->reverbBus, e->reverbLevel);
  } else if (spec && !spec->reverb.empty()) {
    ApplyReverb(*e, spec->reverb, spec->reverbLevel);
  }
  Prune(*e);
  auto* feed = AudioFeed::Get();
  const uint32_t playingId = s_postEvent(WwiseHash(type), e->object, 0, nullptr, nullptr, 0, nullptr, 0);
  if (playingId == 0) {
    plugin->Warn("emitter '" + aName + "': could not start '" + aRow + "' (" + type + ")");
    return false;
  }
  if (!feed->Bind(playingId, row)) {
    plugin->Warn("emitter '" + aName + "': voice table full, '" + aRow + "' will be silent");
    return false;
  }
  e->voices.push_back(playingId);
  return true;
}

bool Emitters::Stop(const std::string& aName, const std::string& aRow, float aFadeOut) {
  if (!m_ready) return false;
  auto* reg = SoundRegistry::Get();
  const uint16_t row = aRow.empty() ? 0xFFFF : reg->RowFor(aRow);
  std::lock_guard lock(m_mutex);
  Emitter* e = Find(aName);
  if (!e) return false;
  auto* feed = AudioFeed::Get();
  bool any = false;
  for (uint32_t pid : e->voices) {
    if (row != 0xFFFF && feed->BoundRow(pid) != row) continue;
    feed->StopVoice(pid, aFadeOut);
    any = true;
  }
  return any;
}

bool Emitters::Destroy(const std::string& aName) {
  if (!m_ready) return false;
  std::lock_guard lock(m_mutex);
  auto it = m_emitters.find(Lower(aName));
  if (it == m_emitters.end()) return false;
  auto* feed = AudioFeed::Get();
  for (uint32_t pid : it->second.voices) feed->Unbind(pid);
  feed->RequestSweep();
  
  s_unregisterGameObj(it->second.object);
  m_emitters.erase(it);
  return true;
}

int32_t Emitters::DestroyAll() {
  if (!m_ready) return 0;
  std::lock_guard lock(m_mutex);
  auto* feed = AudioFeed::Get();
  int32_t n = 0;
  for (auto& kv : m_emitters) {
    for (uint32_t pid : kv.second.voices) feed->Unbind(pid);
    s_unregisterGameObj(kv.second.object);
    ++n;
  }
  if (n > 0) feed->RequestSweep();
  m_emitters.clear();
  return n;
}

bool Emitters::Has(const std::string& aName) const {
  std::lock_guard lock(m_mutex);
  return m_emitters.count(Lower(aName)) != 0;
}

int32_t Emitters::Count() const {
  std::lock_guard lock(m_mutex);
  return static_cast<int32_t>(m_emitters.size());
}

}  
