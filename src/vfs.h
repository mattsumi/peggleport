#pragma once
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace pp {

std::string normKey(std::string_view path);
std::string genericUtf8(const std::filesystem::path& p);
std::map<std::string, std::string> readIni(const std::string& text);

struct PakEntry {
  std::string name;
  uint32_t offset = 0, size = 0;
  uint64_t filetime = 0;
};
struct Pak {
  std::vector<uint8_t> data;
  std::vector<PakEntry> entries;
};
bool loadPak(const std::filesystem::path& p, Pak& out, std::string* err);

struct Layer {
  enum Kind { Dir, PakFile } kind = Dir;
  std::string label;
  bool isMod = false, isLevelPack = false;
  int priority = 0;
  float imageScale = 1.0f;
  std::filesystem::path root;
  std::shared_ptr<Pak> pak;
  std::map<std::string, uint32_t> pakIndex;
  std::map<std::string, std::filesystem::path> files;
  std::map<std::string, std::string> meta;
};

struct VfsHit {
  std::vector<uint8_t> bytes;
  const Layer* layer = nullptr;
};

struct VfsChange {
  bool any = false;
  std::set<std::string> keys;
  bool touches(std::string_view path) const;
  bool touchesFolder(std::string_view folder) const;
  bool touchesImage(std::string_view path) const;
};

void vfsLogMiss(const char* kind, std::string_view path);

class Vfs {
 public:
  bool mountGame(const std::filesystem::path& gameDir, std::string* err);
  bool mountMod(const std::filesystem::path& modDir, std::string* err);
  bool unmountMod(const std::filesystem::path& modDir);
  size_t unmountMods();
  std::string modSignature() const;
  void modKeys(std::set<std::string>& out) const;
  std::optional<VfsHit> read(std::string_view path) const;
  std::optional<VfsHit> read(std::string_view path, std::size_t maxBytes) const;
  bool exists(std::string_view path) const;
  std::vector<std::string> keys() const;
  const std::vector<Layer>& layers() const { return layers_; }
  std::string describe() const;

 private:
  static void indexDir(Layer& L);
  std::vector<Layer> layers_;
};

}
