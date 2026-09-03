#include "AudioXLPlugin.hpp"

namespace AudioXLNS {

AudioXLPlugin* AudioXLPlugin::Get() {
  static AudioXLPlugin instance;
  return &instance;
}

void AudioXLPlugin::Load(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle) {
  m_sdk = aSdk;
  m_handle = aHandle;
}

void AudioXLPlugin::Unload() {
  m_sdk = nullptr;
  m_handle = nullptr;
}

void AudioXLPlugin::Info(const std::string& aMessage) const {
  if (!m_sdk) return;
  std::lock_guard lock(m_logMutex);
  m_sdk->logger->Info(m_handle, aMessage.c_str());
}

void AudioXLPlugin::Warn(const std::string& aMessage) const {
  if (!m_sdk) return;
  std::lock_guard lock(m_logMutex);
  m_sdk->logger->Warn(m_handle, aMessage.c_str());
}

void AudioXLPlugin::Error(const std::string& aMessage) const {
  if (!m_sdk) return;
  std::lock_guard lock(m_logMutex);
  m_sdk->logger->Error(m_handle, aMessage.c_str());
}

}  
