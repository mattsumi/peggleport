#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "mods/manifest.h"
#include "mods/modapi.h"

namespace pp::mods {

struct NativeStatus {
  uint32_t protections = 0;
  bool probed = false;
  bool helperMissing = false;
  bool needsUnrestricted = false;
  std::string detail;
  bool full() const;
};

struct NativeOptions {
  std::filesystem::path dir;
  bool allowUnrestricted = false;
  NativeStatus* status = nullptr;
};

class NativeMod {
 public:
  static std::unique_ptr<NativeMod> load(const std::filesystem::path& lib, const Manifest& m, HostServices& host, std::string* err,
                                         const NativeOptions& opt = NativeOptions());
  static NativeStatus probe();
  static std::string protectionText(uint32_t protections);
  static std::string missingText(uint32_t protections);
  ~NativeMod();
  void levelStart();
  void tick();
  void event(int type, int a, int b, int c, int ball);
  bool power(int pegIdx, int power);
  void frame();
  bool dead() const;
  const std::string& deadReason() const;
  uint32_t protections() const;
  struct Impl;

 private:
  NativeMod() = default;
  std::unique_ptr<Impl> im_;
};

}
