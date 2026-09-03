#ifndef AUDIOXL_PLUGIN_HPP
#define AUDIOXL_PLUGIN_HPP

#include <mutex>
#include <string>

#include <RED4ext/RED4ext.hpp>

namespace AudioXLNS {

class AudioXLPlugin {
 public:
  static AudioXLPlugin* Get();

  void Load(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle);
  void Unload();

  void Info(const std::string& aMessage) const;
  void Warn(const std::string& aMessage) const;
  void Error(const std::string& aMessage) const;

  bool IsLoaded() const { return m_sdk != nullptr; }
  const RED4ext::v1::Sdk* Sdk() const { return m_sdk; }
  RED4ext::v1::PluginHandle Handle() const { return m_handle; }

 private:
  AudioXLPlugin() = default;

  const RED4ext::v1::Sdk* m_sdk = nullptr;
  RED4ext::v1::PluginHandle m_handle = nullptr;
  mutable std::mutex m_logMutex;
};

}  

#endif  
