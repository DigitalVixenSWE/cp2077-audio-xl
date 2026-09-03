#include <RED4ext/RED4ext.hpp>
#include <RedLib.hpp>

#include <cstdint>

#include "AudioXLNatives.hpp"
#include "AudioXLPlugin.hpp"
#include "Config.h"
#include "SoundRegistry.hpp"

namespace AudioXLNS {

namespace {

constexpr uintptr_t kRvaAudioInit = 0xA2E9FC;

using AudioInitFn = uint8_t(__fastcall*)(void* aSelf, wchar_t* aArg, void* aArg2);
AudioInitFn s_original = nullptr;
void* s_hookTarget = nullptr;

uint8_t __fastcall OnAudioInit(void* aSelf, wchar_t* aArg, void* aArg2) {
  const uint8_t result = s_original(aSelf, aArg, aArg2);
  SoundRegistry::Get()->OnAudioInitDone();
  return result;
}

void AttachHook() {
  auto* plugin = AudioXLPlugin::Get();
  if (!SoundRegistry::Get()->Available()) {
    return;   
  }
  const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  s_hookTarget = reinterpret_cast<void*>(base + kRvaAudioInit);
  const bool ok = plugin->Sdk()->hooking->Attach(plugin->Handle(), s_hookTarget,
                                                 reinterpret_cast<void*>(&OnAudioInit),
                                                 reinterpret_cast<void**>(&s_original));
  if (!ok) {
    
    plugin->Warn("audio init hook attach failed; early registrations will wait for the first script call");
    s_hookTarget = nullptr;
    return;
  }

}

void DetachHook() {
  auto* plugin = AudioXLPlugin::Get();
  if (s_hookTarget && plugin->Sdk()) {
    plugin->Sdk()->hooking->Detach(plugin->Handle(), s_hookTarget);
    s_hookTarget = nullptr;
  }
}

}  

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::v1::PluginHandle aHandle,
                                        RED4ext::v1::EMainReason aReason,
                                        const RED4ext::v1::Sdk* aSdk) {
  switch (aReason) {
    case RED4ext::v1::EMainReason::Load: {
      Red::TypeInfoRegistrar::RegisterDiscovered();
      AudioXLPlugin::Get()->Load(aSdk, aHandle);
      SoundRegistry::Get()->Init();
      if (!SoundRegistry::Get()->Available()) {
        AudioXLPlugin::Get()->Error("AudioXL disabled: " + SoundRegistry::Get()->Status());
      }
      AttachHook();
      break;
    }
    case RED4ext::v1::EMainReason::Unload: {
      DetachHook();
      AudioXLPlugin::Get()->Unload();
      break;
    }
  }
  return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo) {
  aInfo->name = PLUGIN_NAME;
  aInfo->author = PLUGIN_AUTHOR;
  aInfo->version = RED4EXT_V1_SEMVER(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
  
  aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_LATEST;
  aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports() {
  return RED4EXT_API_VERSION_1;
}

}  
