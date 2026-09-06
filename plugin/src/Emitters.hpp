#ifndef AUDIOXL_EMITTERS_HPP
#define AUDIOXL_EMITTERS_HPP

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace AudioXLNS {

class Emitters {
 public:
  static Emitters* Get();

  bool Init();
  bool Available() const { return m_ready; }
  const std::string& Status() const { return m_status; }

  bool Create(const std::string& aName, float aX, float aY, float aZ);
  bool Move(const std::string& aName, float aX, float aY, float aZ);
  
  bool SetReverb(const std::string& aName, const std::string& aBus, float aLevel);
  bool Play(const std::string& aName, const std::string& aRow);
  
  bool Stop(const std::string& aName, const std::string& aRow, float aFadeOut);
  bool Destroy(const std::string& aName);
  int32_t DestroyAll();
  bool Has(const std::string& aName) const;
  int32_t Count() const;

 private:
  Emitters() = default;

  struct Emitter {
    std::string name;
    uint64_t object = 0;
    float pos[3] = {0.0f, 0.0f, 0.0f};
    std::string reverbBus;        
    float reverbLevel = 0.0f;
    uint32_t appliedBus = 0;      
    float appliedLevel = 0.0f;
    std::vector<uint32_t> voices; 
  };

  Emitter* Find(const std::string& aName);
  bool SetPosition(Emitter& aEmitter);
  void ApplyReverb(Emitter& aEmitter, const std::string& aBus, float aLevel);
  void Prune(Emitter& aEmitter);

  bool m_ready = false;
  std::string m_status;
  uint64_t m_nextObject = 0;
  std::map<std::string, Emitter> m_emitters;   
  mutable std::mutex m_mutex;
};

}  

#endif  
