#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "mods/manifest.h"
#include "mods/modapi.h"

namespace pp::mods {

struct NativeMasterDef {
  std::string id, name, description, powerName, powerDescription, portrait, banner;
  int turns = 3;
  int trigger = 0;
  uint32_t color = 0x88ff88;
  void (*onActivate)(int32_t, int32_t) = nullptr;
  void (*onShot)(int32_t) = nullptr;
  void (*onEnd)(int32_t) = nullptr;
};

class NativeMod {
 public:
  static std::unique_ptr<NativeMod> load(const std::filesystem::path& lib, const Manifest& m, HostServices& host, std::string* err);
  ~NativeMod();
  void levelStart();
  void tick();
  void event(int type, int a, int b, int c, int ball);
  bool power(int pegIdx, int power);
  void frame();
  const std::vector<NativeMasterDef>& masters() const;
  void masterCall(const NativeMasterDef& m, int which, int player, int peg);
  struct Impl;

 private:
  NativeMod() = default;
  std::unique_ptr<Impl> im_;
};

}
