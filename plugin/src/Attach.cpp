#include "Attach.hpp"

#include <algorithm>
#include <cctype>

#include "AudioXLPlugin.hpp"
#include "Emitters.hpp"

namespace AudioXLNS {

namespace {

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool WorldPosition(RED4ext::IScriptable* aEntity, float& aX, float& aY, float& aZ) {
  if (!aEntity) return false;
  RED4ext::Vector4 pos{};
  if (!Red::CallVirtual(aEntity, "GetWorldPosition", pos)) return false;
  aX = pos.X;
  aY = pos.Y;
  aZ = pos.Z;
  return true;
}

}  

AttachedEmitters* AttachedEmitters::Get() {
  static AttachedEmitters instance;
  return &instance;
}

bool AttachedEmitters::Attach(const std::string& aName, const Red::Handle<RED4ext::IScriptable>& aEntity) {
  if (aName.empty() || !aEntity) return false;
  const std::string key = Lower(aName);

  float x = 0.0f, y = 0.0f, z = 0.0f;
  const bool havePos = WorldPosition(aEntity.instance, x, y, z);

  auto* emitters = Emitters::Get();
  if (!emitters->Has(key)) {
    
    if (!havePos || !emitters->Create(key, x, y, z)) return false;
  } else if (havePos) {
    emitters->Move(key, x, y, z);
  }

  for (auto& bond : m_bonds) {
    if (bond.name == key) {
      bond.entity = aEntity;   
      return true;
    }
  }
  Bond bond;
  bond.name = key;
  bond.entity = aEntity;
  m_bonds.push_back(bond);
  return true;
}

bool AttachedEmitters::Detach(const std::string& aName) {
  const std::string key = Lower(aName);
  for (size_t i = 0; i < m_bonds.size(); ++i) {
    if (m_bonds[i].name == key) {
      m_bonds.erase(m_bonds.begin() + static_cast<long>(i));
      return true;
    }
  }
  return false;
}

bool AttachedEmitters::IsAttached(const std::string& aName) const {
  const std::string key = Lower(aName);
  for (const auto& bond : m_bonds) {
    if (bond.name == key) return true;
  }
  return false;
}

int32_t AttachedEmitters::Count() const { return static_cast<int32_t>(m_bonds.size()); }

int32_t AttachedEmitters::Tick() {
  if (m_bonds.empty()) return 0;
  auto* emitters = Emitters::Get();
  int32_t moved = 0;

  for (size_t i = 0; i < m_bonds.size();) {
    auto handle = m_bonds[i].entity.Lock();
    if (!handle) {
      
      emitters->Destroy(m_bonds[i].name);
      m_bonds.erase(m_bonds.begin() + static_cast<long>(i));
      continue;
    }
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (WorldPosition(handle.instance, x, y, z) && emitters->Move(m_bonds[i].name, x, y, z)) {
      ++moved;
    }
    ++i;
  }
  return moved;
}

}  
