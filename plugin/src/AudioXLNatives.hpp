#ifndef AUDIOXL_NATIVES_HPP
#define AUDIOXL_NATIVES_HPP

#include <RED4ext/RED4ext.hpp>
#include <RedLib.hpp>

#include "Attach.hpp"
#include "AudioFeed.hpp"
#include "Emitters.hpp"
#include "Manifest.hpp"
#include "SoundRegistry.hpp"

namespace AudioXLNS {

class AudioXLNative : public Red::IScriptable {
 public:
  static bool Available() { return SoundRegistry::Get()->Available(); }

  static bool AttachEmitter(Red::CName aName, const Red::Handle<RED4ext::IScriptable>& aEntity) {
    return AttachedEmitters::Get()->Attach(aName.ToString(), aEntity);
  }
  
  static bool DetachEmitter(Red::CName aName) {
    return AttachedEmitters::Get()->Detach(aName.ToString());
  }
  static bool IsEmitterAttached(Red::CName aName) {
    return AttachedEmitters::Get()->IsAttached(aName.ToString());
  }
  static int32_t AttachedEmitterCount() { return AttachedEmitters::Get()->Count(); }
  
  static int32_t TickEmitters() { return AttachedEmitters::Get()->Tick(); }

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

  static bool PlayFrom(Red::CName aName, float aSeconds) {
    auto* reg = SoundRegistry::Get();
    const uint16_t row = reg->RowFor(aName.ToString());
    if (row == 0xFFFF) return false;
    const RowFormat* fmt = reg->FormatForRow(row);
    if (!fmt || fmt->sampleRate == 0) return false;
    const double frames = static_cast<double>(aSeconds < 0.0f ? 0.0f : aSeconds) * fmt->sampleRate;
    AudioFeed::Get()->SetStartFrame(row, static_cast<uint64_t>(frames));
    return true;
  }

  static float Pause(Red::CName aName, float aFadeOut) {
    auto* reg = SoundRegistry::Get();
    const uint16_t row = reg->RowFor(aName.ToString());
    if (row == 0xFFFF) return 0.0f;
    const RowFormat* fmt = reg->FormatForRow(row);
    if (!fmt || fmt->sampleRate == 0) return 0.0f;
    auto* feed = AudioFeed::Get();
    const uint64_t at = feed->PositionFrame(row);
    feed->Stop(row, aFadeOut);          
    feed->SetStartFrame(row, at);       
    return static_cast<float>(static_cast<double>(at) / fmt->sampleRate);
  }

  static float Position(Red::CName aName) {
    auto* reg = SoundRegistry::Get();
    const uint16_t row = reg->RowFor(aName.ToString());
    if (row == 0xFFFF) return 0.0f;
    const RowFormat* fmt = reg->FormatForRow(row);
    if (!fmt || fmt->sampleRate == 0) return 0.0f;
    const uint64_t at = AudioFeed::Get()->PositionFrame(row);
    return static_cast<float>(static_cast<double>(at) / fmt->sampleRate);
  }

  static uint32_t WwiseId(Red::CName aName) { return WwiseHash(aName.ToString()); }

  static bool Enabled() { return SoundRegistry::Get()->Enabled(); }

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

  static bool CreateEmitter(Red::CName aName, float aX, float aY, float aZ) {
    return Emitters::Get()->Create(aName.ToString(), aX, aY, aZ);
  }
  static bool MoveEmitter(Red::CName aName, float aX, float aY, float aZ) {
    return Emitters::Get()->Move(aName.ToString(), aX, aY, aZ);
  }
  
  static bool SetEmitterReverb(Red::CName aName, Red::CName aBus, float aLevel) {
    return Emitters::Get()->SetReverb(aName.ToString(), aBus.ToString(), aLevel);
  }
  static bool PlayOn(Red::CName aEmitter, Red::CName aSound) {
    return Emitters::Get()->Play(aEmitter.ToString(), aSound.ToString());
  }
  
  static bool StopOn(Red::CName aEmitter, Red::CName aSound, float aFadeOut) {
    const std::string row = aSound.IsNone() ? std::string() : std::string(aSound.ToString());
    return Emitters::Get()->Stop(aEmitter.ToString(), row, aFadeOut);
  }
  static bool DestroyEmitter(Red::CName aName) { return Emitters::Get()->Destroy(aName.ToString()); }
  static int32_t DestroyAllEmitters() { return Emitters::Get()->DestroyAll(); }
  static bool HasEmitter(Red::CName aName) { return Emitters::Get()->Has(aName.ToString()); }
  static int32_t EmitterCount() { return Emitters::Get()->Count(); }

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
  RTTI_METHOD(PlayFrom);
  RTTI_METHOD(Pause);
  RTTI_METHOD(Enabled);
  RTTI_METHOD(Position);
  RTTI_METHOD(LoadBank);
  RTTI_METHOD(RetryBanks);
  RTTI_METHOD(Duration);
  RTTI_METHOD(Subtitle);
  RTTI_METHOD(Speaker);
  RTTI_METHOD(WwiseId);
  RTTI_METHOD(Has);
  RTTI_METHOD(Count);
  RTTI_METHOD(Report);
  RTTI_METHOD(CreateEmitter);
  RTTI_METHOD(MoveEmitter);
  RTTI_METHOD(SetEmitterReverb);
  RTTI_METHOD(PlayOn);
  RTTI_METHOD(StopOn);
  RTTI_METHOD(DestroyEmitter);
  RTTI_METHOD(DestroyAllEmitters);
  RTTI_METHOD(HasEmitter);
  RTTI_METHOD(EmitterCount);
  RTTI_METHOD(AttachEmitter);
  RTTI_METHOD(DetachEmitter);
  RTTI_METHOD(IsEmitterAttached);
  RTTI_METHOD(AttachedEmitterCount);
  RTTI_METHOD(TickEmitters);
});

#endif  
