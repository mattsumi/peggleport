#include "masters.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "mods/hostreg.h"

namespace pp::masters {

namespace {

std::vector<ModMaster>& table() {
  static std::vector<ModMaster> t;
  return t;
}

std::string lower(const std::string& s) {
  std::string o;
  for (char c : s) o.push_back((char)std::tolower((unsigned char)c));
  return o;
}

}

void install(std::vector<ModMaster> list) {
  std::sort(list.begin(), list.end(), [](const ModMaster& a, const ModMaster& b) { return a.modId != b.modId ? a.modId < b.modId : a.id < b.id; });
  if ((int)list.size() > kMaxMods) list.resize((size_t)kMaxMods);
  for (size_t i = 0; i < list.size(); i++) list[i].power = kFirstPower + (int)i;
  table() = std::move(list);
}

void clear() { table().clear(); }

const std::vector<ModMaster>& all() { return table(); }

const ModMaster* byPower(int power) {
  const int i = power - kFirstPower;
  return i >= 0 && i < (int)table().size() ? &table()[(size_t)i] : nullptr;
}

const ModMaster* byKey(const std::string& key) {
  for (auto& m : table())
    if (m.key() == key) return &m;
  return nullptr;
}

const ModMaster* byName(const std::string& name) {
  const std::string n = lower(name);
  if (n.empty()) return nullptr;
  for (auto& m : table())
    if (lower(m.key()) == n) return &m;
  for (auto& m : table())
    if (lower(m.id) == n) return &m;
  return nullptr;
}

bool reservedName(const std::string& id) { return modhost::reservedMasterName(id); }

std::string initial(const ModMaster& m) {
  for (char c : m.name)
    if (std::isalnum((unsigned char)c)) return std::string(1, (char)std::toupper((unsigned char)c));
  return "?";
}

}
