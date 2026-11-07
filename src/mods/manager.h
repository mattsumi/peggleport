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

namespace pp {
class Vfs;
}

namespace pp::mods {

struct ModInfo {
  Manifest m;
  std::filesystem::path dir, source;
  uint64_t hash = 0;
  std::string hashHex;
  bool fromCli = false, enabled = false, zip = false;
  ScanReport scan;
  bool consented = false;
  enum State { Off, NeedsConsent, Active, Failed } state = Off;
  std::string error;
  std::unique_ptr<LuaMod> lua;
  std::unique_ptr<NativeMod> native;
  bool hasCode() const { return !m.scripts.empty() || !m.native.empty(); }
  bool needsConsent() const { return scan.needsConsent() && !consented; }
  const char* stateName() const;
};

class ModManager final : public SimHooks {
 public:
  ModManager();
  ~ModManager() override;

  void setUserDir(const std::filesystem::path& userDir);
  void discover(const std::vector<std::string>& cliMods = {});
  void activate(Vfs* vfs);
  void deactivate();
  std::vector<std::unique_ptr<ModInfo>>& mods() { return mods_; }
  ModInfo* find(const std::string& id);
  bool setEnabled(const std::string& id, bool on);
  bool setConsent(const std::string& id, bool grant);
  void move(const std::string& id, int delta);
  bool save() const;
  uint64_t setHash() const { return setHash_; }
  uint64_t computeSetHash() const;
  std::filesystem::path modsDir() const { return userDir_ / "mods"; }
  int activeCount() const;
  bool anyPendingConsent() const;
  std::string report() const;

  void frame(DrawSink& sink, const Board& b);
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

  void flush(Board& b);

 private:
  struct Scope;
  std::filesystem::path userDir_;
  std::vector<std::unique_ptr<ModInfo>> mods_;
  std::vector<std::string> order_, messages_;
  std::map<std::string, std::string> consent_;
  std::vector<ModCmd> queue_;
  HostServices host_;
  uint64_t setHash_ = 0;
  bool installed_ = false;
  bool queueFull_ = false;
  void logDiscovered(const ModInfo& mi);
  void loadConfig(std::vector<std::string>& enabled);
  void addMod(const std::filesystem::path& src, bool cli);
  void apply(Board& b, const ModCmd& c);
  template <class F> void eachSim(F&& f);
};

inline ModManager* g_mods = nullptr;

}
