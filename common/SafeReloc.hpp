#pragma once

#include <Windows.h>

#include <cstdint>

namespace DV {

inline uintptr_t ResolveAddress(uint32_t aHash) {
  using ResolveFn = uintptr_t (*)(uint32_t);
  static const ResolveFn resolve = []() -> ResolveFn {
    const HMODULE red4ext = GetModuleHandleW(L"RED4ext.dll");
    if (!red4ext) {
      return nullptr;
    }
    return reinterpret_cast<ResolveFn>(GetProcAddress(red4ext, "RED4ext_ResolveAddress"));
  }();
  return resolve ? resolve(aHash) : 0;
}

template <typename T>
inline T SafeRelocFunc(uint32_t aHash) {
  return reinterpret_cast<T>(ResolveAddress(aHash));
}

}  
