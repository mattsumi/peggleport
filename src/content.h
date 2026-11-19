#pragma once
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

#include "level.h"
#include "sim.h"
#include "vfs.h"

namespace pp {

uint64_t fnv1a(const void* p, size_t n, uint64_t h = 1469598103934665603ull);

struct BaseLink {
  std::string name, hash;
};

bool readBaseLevel(const Vfs& vfs, BaseLink& link, std::shared_ptr<const Level>& level, uint64_t& hash, std::string* err);

struct LevelBundle {
  BaseLink baseLink;
  std::shared_ptr<const Level> base, level;
  std::string name, title, background, source, master;
  BoardConfig cfg;
  uint64_t contentHash = 0;
};

struct StageLevel {
  std::string name, title, pack;
  int stage = 0;
};

struct ImageRes {
  std::string id, path, alphaImage;
  int rows = 1, cols = 1;
  bool alphaColor = false;
  uint32_t alphaColorRGB = 0;
  bool animOnce = false;
  int frameDelay = 0;
};
class ImageManifest {
 public:
  explicit ImageManifest(const Vfs& v);
  const ImageRes* find(const std::string& id) const;
  size_t size() const { return res_.size(); }
  const std::map<std::string, ImageRes>& images() const { return res_; }
 private:
  std::map<std::string, ImageRes> res_;
};
int stageOfLevel(const Vfs& vfs, const std::string& levelName);

struct CharInfo {
  std::string name, power, image, eyeImage;
  int frames = 3, posX = 0, posY = 0, tween = 0;
  std::vector<std::array<int, 4>> eyes;
};
std::vector<CharInfo> loadCharacters(const Vfs& vfs);

bool loadLevelBundle(const Vfs& vfs, const std::string& nameOrPath, LevelBundle& out, std::string* err);
std::vector<StageLevel> listStageLevels(const Vfs& vfs);

}
