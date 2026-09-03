#ifndef AUDIOXL_MANIFEST_HPP
#define AUDIOXL_MANIFEST_HPP

#include <string>

namespace AudioXLNS {

class SoundRegistry;

class Manifest {
 public:
  
  static void LoadAll(SoundRegistry& aRegistry);

  static int LoadFile(SoundRegistry& aRegistry, const std::string& aPath);
};

}  

#endif  
