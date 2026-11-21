#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

#include "mods/luamod.h"
#include "mods/manifest.h"
#include "mods/modapi.h"
#include "mods/native.h"
#include "mods/scanner.h"
#include "mods/simhooks.h"
#include "vfs.h"

namespace pp::mods {

struct ModInfo {
  Manifest m;
  std::filesystem::path dir, source;
  uint64_t hash = 0;
  std::string hashHex;
  bool fromCli = false, enabled = false, zip = false, discoveryFailed = false, failedOnLoad = false;
  ScanReport scan;
  bool consented = false;
  bool unrestricted = false;
  bool needsUnrestricted = false;
  uint32_t protections = 0;
  bool helperRuns = false;
  enum State { Off, NeedsConsent, Active, Failed } state = Off;
  std::string error;
  std::unique_ptr<LuaMod> lua;
  std::unique_ptr<NativeMod> native;
  bool hasCode() const { return !m.scripts.empty() || !m.native.empty(); }
  bool needsConsent() const { return scan.needsConsent() && !consented; }
  bool willLoad() const { return !discoveryFailed && enabled && !needsConsent(); }
  std::string protectionLine() const;
  const char* stateName() const;
};

struct ReloadResult {
  bool skipped = false, rescanned = false, changed = false;
  VfsChange assets;
  int active = 0, total = 0, failed = 0;
  long long ms = 0;
};

class ModManager final : public SimHooks {
 public:
  ModManager();
  ~ModManager() override;

  void setUserDir(const std::filesystem::path& userDir);
  void discover(const std::vector<std::string>& cliMods = {});
  void activate(Vfs* vfs);
  void deactivate();
  ReloadResult reload(Vfs& vfs);
  bool folderChanged() const;
  std::string statusText() const;
  std::vector<std::unique_ptr<ModInfo>>& mods() { return mods_; }
  ModInfo* find(const std::string& id);
  bool setEnabled(const std::string& id, bool on);
  bool setConsent(const std::string& id, bool grant);
  bool setUnrestricted(const std::string& id, bool grant);
  bool needsUnrestrictedStep(const ModInfo& m) const;
  void move(const std::string& id, int delta);
  bool save() const;
  uint64_t setHash() const { return setHash_; }
  uint64_t computeSetHash() const;
  std::filesystem::path modsDir() const { return userDir_ / "mods"; }
  int activeCount() const;
  bool anyPendingConsent() const;
  std::string report() const;

  void frame(DrawSink& sink, const Board& b);
  void controlTick(const std::function<std::string(const std::string&)>& handler);
  void pump();
  struct MenuRef { std::string modId, label; int index; };
  std::vector<MenuRef> menus() const;
  bool runMenu(const std::string& modId, int index);
  std::vector<std::string> takeMessages();

  void levelStart(Board& b) override;
  void stepBegin(Board& b) override;
  void stepEnd(Board& b) override;
  void event(Board& b, int type, int a, int bb, int c, int ball) override;
  bool power(Board& b, int pegIdx, int power, bool silent) override;
  void basePhysics(PhysicsParams& p) override;
  void pegCustom(Board& b, int pegIdx, int what, int ball, int custom) override;
  void masterCall(Board& b, int player, int power, int what, int pegIdx) override;

  void flush(Board& b);

 private:
  struct Scope;
  std::filesystem::path userDir_;
  std::vector<std::unique_ptr<ModInfo>> mods_;
  std::vector<std::string> order_, messages_;
  std::map<std::string, std::string> consent_, unrestricted_;
  std::vector<ModCmd> queue_;
  HostServices host_;
  std::vector<std::string> cli_;
  std::string folderSig_, appliedPlan_;
  uint64_t setHash_ = 0;
  bool installed_ = false, activated_ = false;
  bool queueFull_ = false;
  std::string folderSignature() const;
  std::string loadPlan() const;
  void rediscover();
  void settleStates();
  void failLoad(ModInfo& m, const std::string& err);
  struct PegOwner {
    ModInfo* mod = nullptr;
    int hitRef = 0, clearRef = 0;
    const NativeMod::PegTypeReg* nat = nullptr;
  };
  PegTypeTable pegTable_;
  std::vector<PegOwner> pegOwners_;
  void buildPegTypes();
  struct MasterBind {
    ModInfo* mi = nullptr;
    bool native = false;
    size_t idx = 0;
  };
  std::vector<MasterBind> binds_;
  void installMasters();
  void refreshTypeSig();
  void checkNative(ModInfo& m);
  void logDiscovered(const ModInfo& mi);
  void loadConfig(std::vector<std::string>& enabled);
  void addMod(const std::filesystem::path& src, bool cli);
  void apply(Board& b, const ModCmd& c);
  template <class F> void eachSim(F&& f);
};

inline ModManager* g_mods = nullptr;

}
