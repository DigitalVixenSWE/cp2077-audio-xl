#ifndef AUDIOXL_ATTACH_HPP
#define AUDIOXL_ATTACH_HPP

// Emitters that follow an entity. (๑•̀ㅂ•́)و

#include <cstdint>
#include <string>
#include <vector>

#include <RED4ext/RED4ext.hpp>
#include <RedLib.hpp>

namespace AudioXLNS {

class AttachedEmitters {
 public:
  static AttachedEmitters* Get();

  bool Attach(const std::string& aName, const Red::Handle<RED4ext::IScriptable>& aEntity);
  
  bool Detach(const std::string& aName);
  bool IsAttached(const std::string& aName) const;
  int32_t Count() const;

  int32_t Tick();

 private:
  AttachedEmitters() = default;

  struct Bond {
    std::string name;                                  
    Red::WeakHandle<RED4ext::IScriptable> entity;      
  };

  std::vector<Bond> m_bonds;
};

}  

#endif  
