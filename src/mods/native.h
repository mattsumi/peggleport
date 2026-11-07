#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "mods/manifest.h"
#include "mods/modapi.h"
#include "pegtypes.h"

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
  struct PegTypeReg {
    PegTypeDef def;
    void (*onHit)(int32_t, int32_t) = nullptr;
    void (*onClear)(int32_t) = nullptr;
  };
  const std::vector<PegTypeReg>& pegTypes() const;
  void pegHook(const PegTypeReg& r, bool clear, int peg, int ball);
  struct Impl;

 private:
  NativeMod() = default;
  std::unique_ptr<Impl> im_;
};

}
