#ifndef AUDIOXL_DECODE_HPP
#define AUDIOXL_DECODE_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace AudioXLNS {

bool DecodeToWav(const std::string& aExt, const std::vector<uint8_t>& aIn, std::vector<uint8_t>& aOut,
                 std::string& aWhy);

}  

#endif  
