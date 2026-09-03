#ifndef AUDIOXL_NATIVES_HPP
#define AUDIOXL_NATIVES_HPP

#include <RED4ext/RED4ext.hpp>
#include <RedLib.hpp>

#include "AudioFeed.hpp"
#include "Manifest.hpp"
#include "SoundRegistry.hpp"

namespace AudioXLNS {

class AudioXLNative : public Red::IScriptable {
 public:
  static bool Available() { return SoundRegistry::Get()->Available(); }

  static Red::CString Status() { return Red::CString(SoundRegistry::Get()->Status().c_str()); }

  static bool RegisterSound(Red::CName aName, Red::CName aType, const Red::CString& aPath, float aGain,
                            float aPitch, float aDistance) {
    SoundSpec s;
    s.name = aName.ToString();
    s.type = aType.ToString();
    s.path = aPath.c_str();
    s.gain = aGain;
    s.pitch = aPitch;
    s.distance = aDistance;
    return SoundRegistry::Get()->Register(s, "script");
  }

  static bool RegisterSoundEx(Red::CName aName, Red::CName aType, const Red::CString& aPath, float aGain,
                              float aPitch, float aDistance, bool aLoop, float aFadeIn, float aFadeOut,
                              float aStart, float aEnd, float aRate, bool aStream) {
    SoundSpec s;
    s.name = aName.ToString();
    s.type = aType.ToString();
    s.path = aPath.c_str();
    s.gain = aGain;
    s.pitch = aPitch;
    s.distance = aDistance;
    s.loop = aLoop;
    s.fadeIn = aFadeIn;
    s.fadeOut = aFadeOut;
    s.start = aStart;
    s.end = aEnd;
    s.rate = aRate > 0.0f ? aRate : 1.0f;
    s.stream = aStream;
    return SoundRegistry::Get()->Register(s, "script");
  }

  static bool Mute(Red::CName aName) { return SoundRegistry::Get()->Mute(aName.ToString(), "script"); }

  static int32_t LoadManifest(const Red::CString& aPath) {
    return Manifest::LoadFile(*SoundRegistry::Get(), aPath.c_str());
  }

  static bool Stop(Red::CName aName, float aFadeOut) {
    const uint16_t row = SoundRegistry::Get()->RowFor(aName.ToString());
    if (row == 0xFFFF) return false;
    AudioFeed::Get()->Stop(row, aFadeOut);
    return true;
  }
  static bool SetGain(Red::CName aName, float aGain) {
    const uint16_t row = SoundRegistry::Get()->RowFor(aName.ToString());
    if (row == 0xFFFF) return false;
    AudioFeed::Get()->SetGain(row, aGain);
    return true;
  }
  static bool IsPlaying(Red::CName aName) {
    const uint16_t row = SoundRegistry::Get()->RowFor(aName.ToString());
    return row != 0xFFFF && AudioFeed::Get()->IsPlaying(row);
  }

  static uint32_t WwiseId(Red::CName aName) {
    uint32_t h = 0x811C9DC5u;
    for (const char* p = aName.ToString(); p && *p; ++p) {
      const char c = (*p >= 'A' && *p <= 'Z') ? static_cast<char>(*p + 32) : *p;
      h = (h * 0x01000193u) ^ static_cast<uint8_t>(c);
    }
    return h;
  }

  static int32_t LoadBank(const Red::CString& aPath) {
    return SoundRegistry::Get()->LoadBank(aPath.c_str(), "script");
  }

  static int32_t RetryBanks() { return SoundRegistry::Get()->RetryBanks(); }

  static float Duration(Red::CName aName) { return SoundRegistry::Get()->Duration(aName.ToString()); }
  static Red::CString Subtitle(Red::CName aName, const Red::CString& aLocale) {
    return Red::CString(SoundRegistry::Get()->Subtitle(aName.ToString(), aLocale.c_str()).c_str());
  }
  static Red::CString Speaker(Red::CName aName) {
    return Red::CString(SoundRegistry::Get()->Speaker(aName.ToString()).c_str());
  }

  static bool Has(Red::CName aName) { return SoundRegistry::Get()->Has(aName.ToString()); }

  static int32_t Count() { return SoundRegistry::Get()->Count(); }

  static Red::DynArray<Red::CString> Report() {
    Red::DynArray<Red::CString> out;
    for (const auto& l : SoundRegistry::Get()->Report()) {
      out.PushBack(Red::CString(l.c_str()));
    }
    return out;
  }

  RTTI_IMPL_TYPEINFO(AudioXLNative);
  RTTI_IMPL_ALLOCATOR();
};

}  

RTTI_DEFINE_CLASS(AudioXLNS::AudioXLNative, "AudioXLNative", {
  RTTI_ALIAS("AudioXL.AudioXLNative");
  RTTI_METHOD(Available);
  RTTI_METHOD(Status);
  RTTI_METHOD(RegisterSound);
  RTTI_METHOD(RegisterSoundEx);
  RTTI_METHOD(Mute);
  RTTI_METHOD(LoadManifest);
  RTTI_METHOD(Stop);
  RTTI_METHOD(SetGain);
  RTTI_METHOD(IsPlaying);
  RTTI_METHOD(LoadBank);
  RTTI_METHOD(RetryBanks);
  RTTI_METHOD(Duration);
  RTTI_METHOD(Subtitle);
  RTTI_METHOD(Speaker);
  RTTI_METHOD(WwiseId);
  RTTI_METHOD(Has);
  RTTI_METHOD(Count);
  RTTI_METHOD(Report);
});

#endif  
