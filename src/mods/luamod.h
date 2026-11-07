#pragma once
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "mods/manifest.h"
#include "mods/modapi.h"
#include "pegtypes.h"

struct lua_State;

namespace pp::mods {

struct MenuEntry {
  std::string label;
  int ref = 0;
};
struct PegTypeReg {
  PegTypeDef def;
  int hitRef = 0, clearRef = 0;
};
struct PowerDef {
  std::string name, description;
  int replaces = 0;
  bool replace = true;
  int ref = 0;
};

struct MasterDef {
  std::string id, name, description, powerName, powerDescription, portrait, banner;
  int turns = 3;
  int trigger = 0;
  uint32_t color = 0x88ff88;
  int onActivate = -1, onShot = -1, onEnd = -1;
};

class LuaMod {
 public:
  LuaMod(const Manifest& m, const std::filesystem::path& dir, HostServices& host);
  ~LuaMod();
  LuaMod(const LuaMod&) = delete;
  LuaMod& operator=(const LuaMod&) = delete;

  bool open(std::string* err);
  bool hasHook(const char* name) const;
  bool callSim(const char* hook, int nargs, const std::function<void(lua_State*)>& push);
  bool callLoose(const char* hook, Ctx ctx, int nargs, const std::function<void(lua_State*)>& push);
  bool runMenu(int index);
  bool callRef(int ref, int nargs, const std::function<void(lua_State*)>& push);
  bool activatePower(const PowerDef& p, int player, int peg);
  bool callPegHook(int ref, int peg, int ball);
  bool callMaster(const MasterDef& m, int which, int player, int peg);
  std::string eval(const std::string& code, std::string* result, bool sim = false);

  Ctx ctx() const;
  const Manifest& manifest() const { return man_; }
  const std::filesystem::path& dir() const { return dir_; }
  HostServices& host() { return host_; }
  std::vector<MenuEntry> menus;
  std::vector<PowerDef> powers;
  PhysicsEdit loadPhysics;
  std::vector<PegTypeReg> pegTypes;
  std::vector<MasterDef> masters;
  size_t memoryUsed() const;
  size_t memoryPeak() const;
  bool memoryDenied() const;
  int errors = 0;
  bool disabled = false;
  long long lastInstructions = 0;
  lua_State* state() const { return L_; }
  std::string lastError;

  struct Impl;
 private:
  friend struct Impl;
  Manifest man_;
  std::filesystem::path dir_;
  HostServices& host_;
  lua_State* L_ = nullptr;
  std::unique_ptr<Impl> im_;
  bool call(const char* hook, Ctx ctx, int nargs, const std::function<void(lua_State*)>& push, bool timed);
  bool pcallBudgeted(int nargs, int nres, bool timed, Ctx ctx, std::string* err);
};

void openNetLib(lua_State* L, LuaMod* mod, HostServices& host);
void pumpNet(lua_State* L);
void closeNet(lua_State* L);

}
