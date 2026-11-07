#pragma once
#include <filesystem>
#include <memory>
#include <string>

#include "mods/manifest.h"
#include "mods/modapi.h"

namespace pp::mods {

class NativeMod {
 public:
  static std::unique_ptr<NativeMod> load(const std::filesystem::path& lib, const Manifest& m, HostServices& host, std::string* err);
  ~NativeMod();
  void levelStart();
  void tick();
  void event(int type, int a, int b, int c, int ball);
  bool power(int pegIdx, int power);
  void frame();
  const PhysicsEdit& loadPhysics() const;
  struct Impl;

 private:
  NativeMod() = default;
  std::unique_ptr<Impl> im_;
};

}
