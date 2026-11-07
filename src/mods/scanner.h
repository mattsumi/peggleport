#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "mods/manifest.h"

namespace pp::mods {

struct Finding {
  enum Kind { NetApi, Url, SocketWord, NativeNet, Dynamic, Native, Fs, Info } kind = Info;
  std::string file;
  int line = 0;
  std::string what;
  bool isNet() const { return kind == NetApi || kind == Url || kind == SocketWord || kind == NativeNet; }
  std::string describe() const;
};

struct ScanReport {
  std::vector<Finding> findings;
  int filesScanned = 0;
  bool declaredNet = false;
  bool hasNative = false;
  bool usesNet() const;
  bool needsConsent() const { return usesNet() || hasNative; }
  std::string summary() const;
  std::string text() const;
};

extern const char* kScanDisclaimer;

ScanReport scanMod(const std::filesystem::path& dir, const Manifest& m);
void scanLuaSource(const std::string& file, std::string_view src, ScanReport& r);
void scanNativeBytes(const std::string& file, std::string_view bytes, ScanReport& r);

}
