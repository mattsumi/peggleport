#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pp {

struct ModMaster {
  enum Trigger { GreenPeg = 0, EveryShot = 1, LevelStart = 2 };
  std::string modId, id, name, description, powerName, powerDescription, banner, portrait, portraitFile;
  int turns = 3;
  int trigger = GreenPeg;
  uint32_t color = 0x88ff88;
  int power = 0;
  std::string key() const { return modId + ":" + id; }
};

namespace masters {

constexpr int kFirstPower = 16;
constexpr int kMaxMods = 48;
constexpr int kPowerSlots = kFirstPower + kMaxMods;

void install(std::vector<ModMaster> list);
void clear();
const std::vector<ModMaster>& all();
const ModMaster* byPower(int power);
const ModMaster* byKey(const std::string& key);
const ModMaster* byName(const std::string& name);
bool reservedName(const std::string& id);
std::string initial(const ModMaster& m);

}

}
