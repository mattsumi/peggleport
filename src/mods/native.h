#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "mods/manifest.h"
#include "mods/modapi.h"
#include "pegtypes.h"

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
  const PhysicsEdit& loadPhysics() const;
  struct PegTypeReg {
    PegTypeDef def;
    void (*onHit)(int32_t, int32_t) = nullptr;
    void (*onClear)(int32_t) = nullptr;
  };
  const std::vector<PegTypeReg>& pegTypes() const;
  void pegHook(const PegTypeReg& r, bool clear, int peg, int ball);
  const std::vector<NativeMasterDef>& masters() const;
  void masterCall(const NativeMasterDef& m, int which, int player, int peg);
  bool dead() const;
  const std::string& deadReason() const;
  uint32_t protections() const;
  struct Impl;

 private:
  NativeMod() = default;
  std::unique_ptr<Impl> im_;
};

}
