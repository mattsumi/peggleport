#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pp {

std::string normKey(std::string_view path);
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

void vfsLogMiss(const char* kind, std::string_view path);

class Vfs {
 public:
  bool mountGame(const std::filesystem::path& gameDir, std::string* err);
  bool mountMod(const std::filesystem::path& modDir, std::string* err);
  std::optional<VfsHit> read(std::string_view path) const;
  bool exists(std::string_view path) const;
  std::vector<std::string> keys() const;
  const std::vector<Layer>& layers() const { return layers_; }
  std::string describe() const;

 private:
  static void indexDir(Layer& L);
  std::vector<Layer> layers_;
};

}
