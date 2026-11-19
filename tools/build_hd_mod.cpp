#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

#include "content.h"
#include "gamepaths.h"
#include "image.h"

namespace fs = std::filesystem;
using namespace pp;

static fs::path assetPath(const std::string& name) {
  std::string key = normKey(name);
  std::replace(key.begin(), key.end(), '\\', '/');
  if (key.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-./") != std::string::npos)
    throw std::runtime_error("Unsupported asset name: " + name);
  fs::path path = pathFromUtf8(key);
  if (path.has_root_path() || !path.has_filename()) throw std::runtime_error("Invalid asset name: " + name);
  for (const auto& part : path) {
    const auto component = pathToUtf8(part);
    const auto stem = component.substr(0, component.find('.'));
    if (component.back() == '.' || component.back() == ' ' || stem == "CON" || stem == "PRN" ||
        stem == "AUX" || stem == "NUL" || (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
        stem.back() >= '0' && stem.back() <= '9')) throw std::runtime_error("Invalid asset name: " + name);
  }
  return path;
}

static void save(const fs::path& file, const Image& image) {
  fs::create_directories(file.parent_path());
  const int w = image.w, h = image.h;
  bool alpha = false;
  for (size_t i = 3; i < image.rgba.size() && !alpha; i += 4) alpha = image.rgba[i] != 255;
  const int stride = alpha ? 4 : 3;
  const uint8_t header[18]{0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    uint8_t(w), uint8_t(w >> 8), uint8_t(h), uint8_t(h >> 8), uint8_t(stride * 8), uint8_t(32 + (alpha ? 8 : 0))};
  std::ofstream output(file, std::ios::binary);
  output.write(reinterpret_cast<const char*>(header), sizeof header);
  std::vector<uint8_t> row((size_t)w * stride);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const uint8_t* src = &image.rgba[((size_t)y * w + x) * 4];
      uint8_t* dst = &row[(size_t)x * stride];
      dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0];
      if (alpha) dst[3] = src[3];
    }
    output.write(reinterpret_cast<const char*>(row.data()), row.size());
  }
  output.close();
  if (!output) throw std::runtime_error("Cannot write " + pathToUtf8(file));
}

static void exportReference(const Vfs& game, const fs::path& output, const std::string& edition) {
  const ImageManifest manifest(game);
  const auto stages = listStageLevels(game);
  if (!manifest.size() || stages.empty()) throw std::runtime_error("No image manifest or level list found");
  std::map<fs::path, ImageRes> assets;
  for (auto& [_, res] : manifest.images()) {
    if (res.path.starts_with("!")) continue;
    auto key = assetPath(res.path);
    if (key.has_extension()) throw std::runtime_error("Unexpected resource extension: " + res.path);
    auto [it, added] = assets.try_emplace(key, res);
    if (!added && (res.rows != it->second.rows || res.cols != it->second.cols || res.alphaImage != it->second.alphaImage))
      throw std::runtime_error("Conflicting atlas metadata for " + res.path);
  }
  std::set<fs::path> backgrounds;
  for (const auto& entry : stages) {
    LevelBundle level;
    std::string error;
    if (!loadLevelBundle(game, entry.name, level, &error)) throw std::runtime_error(error);
    const std::string path = "levels/" + level.background;
    const auto key = assetPath(path);
    if (key.has_extension()) throw std::runtime_error("Unexpected background extension: " + path);
    assets.try_emplace(key, ImageRes{"", path, ""});
    backgrounds.insert(key);
  }
  if (!fs::create_directories(output)) throw std::runtime_error("Output folder already exists");
  for (const auto& [key, info] : assets) {
    auto im = loadGameImage(game, info.path, info.alphaImage);
    if (!im || im->w <= 0 || im->h <= 0 || im->w > 4096 || im->h > 4096)
      throw std::runtime_error("Cannot convert " + info.path);
    auto target = key;
    target += ".tga";
    save(output / target, *im);
  }
  std::ofstream index(output / "art.tsv");
  index << edition << '\n';
  for (const auto& [id, info] : manifest.images()) {
    if (info.path.starts_with("!")) continue;
    const auto key = assetPath(info.path);
    index << id << '\t' << genericUtf8(key) << ".tga\t" << info.rows << '\t' << info.cols << '\n';
  }
  for (const auto& key : backgrounds) index << "BACKGROUND\t" << genericUtf8(key) << ".tga\t1\t1\n";
  index.close();
  if (!index) throw std::runtime_error("Cannot write art.tsv");
}

int main(int argc, char** argv) {
  if (argc != 4 || (std::string(argv[1]) != "Deluxe" && std::string(argv[1]) != "Nights")) {
    std::cerr << "Usage: pp_build_hd_mod <Deluxe|Nights> <game folder with main.pak> <new reference folder>\n";
    return 2;
  }
  try {
    const auto source = fs::canonical(pathFromUtf8(argv[2]));
    const auto output = fs::weakly_canonical(fs::absolute(pathFromUtf8(argv[3])));
    for (auto parent = output; ; parent = parent.parent_path()) {
      if (fs::exists(parent / ".git")) throw std::runtime_error("Choose a new output folder outside any Git checkout");
      if (parent == parent.root_path()) break;
    }
    auto relative = output.lexically_relative(source);
    if (relative.empty() ? output == source : *relative.begin() != "..")
      throw std::runtime_error("Choose a new output folder outside your game install");
    Vfs game;
    std::string error;
    if (!game.mountGame(source, &error)) throw std::runtime_error(error);
    exportReference(game, output, argv[1]);
  } catch (const std::exception& error) {
    std::cerr << "PC artwork was not exported: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
