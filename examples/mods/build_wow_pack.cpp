#include "content.h"
#include "gamecopy.h"
#include "gamepaths.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using Files = std::map<std::string, std::vector<uint8_t>>;

bool copyImage(const pp::Vfs& vfs, Files& files, const std::string& from, const std::string& to) {
  auto family = [](const std::string& path) {
    const auto slash = path.rfind('/');
    const auto start = slash == std::string::npos ? 0 : slash + 1;
    return std::array{path, path.substr(0, start) + '_' + path.substr(start), path + '_'};
  };
  const auto src = family(from), dst = family(to);
  bool copied = false;
  for (size_t i = 0; i < src.size(); ++i) {
    for (const char* ext : {".tga", ".jpg", ".png", ".gif", ".j2k", ".jp2"}) {
      if (files.count(dst[i] + ext)) { copied = true; continue; }
      if (auto hit = vfs.read(src[i] + ext)) {
        files.emplace(dst[i] + ext, std::move(hit->bytes));
        copied = true;
      }
    }
  }
  return copied;
}

std::vector<uint8_t> board(const pp::Vfs& vfs, Files& files, const std::string& key) {
  const auto hit = vfs.read(key);
  if (!hit) throw std::runtime_error("Missing " + key);
  std::string error;
  auto level = pp::parseLevel(hit->bytes, &error);
  if (!level) throw std::runtime_error(key + ": " + error);
  bool changed = false;
  for (auto* node : level->byId) {
    for (const char* field : {"image", "emitter_image", "emitter_image_2"}) {
      auto it = node->fields.find(field);
      if (it == node->fields.end() || it->second.s.empty() || it->second.s == "bumperc") continue;
      auto& value = it->second.s;
      for (const char* prefix : {"", "images/levels/", "images/game/", "levels/"}) {
        const std::string from = prefix + value;
        if (from.find_first_not_of("/abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos || from.starts_with('/')) continue;
        const std::string to = "images/wow_levels/" + from;
        if (!copyImage(vfs, files, from, to)) continue;
        value = to;
        changed = true;
        break;
      }
      if (!value.starts_with("images/wow_levels/")) throw std::runtime_error(key + ": missing object art " + value);
    }
  }
  return changed ? pp::writeLevel(*level) : hit->bytes;
}

void putText(Files& files, const std::string& key, const std::string& text) {
  files.emplace(key, std::vector<uint8_t>(text.begin(), text.end()));
}

void build(const fs::path& source, const fs::path& out) {
  if (pp::foldersOverlap(source, out) || fs::exists(out)) throw std::runtime_error("Choose a new mod folder outside the source installation");
  pp::Vfs vfs;
  std::string error;
  if (!vfs.mountGame(source, &error)) throw std::runtime_error(error);
  auto levels = pp::listStageLevels(vfs);
  if (levels.size() != 10 || levels.front().name != "hogger" || levels.back().name != "lichking")
    throw std::runtime_error("Expected the ten-level World of Warcraft Edition campaign");
  levels.push_back({"hogger1hit", "1-Shot Kill", "main", levels.back().stage + 1});

  Files files;
  files.emplace("levels/_wow_base.dat", board(vfs, files, "levels/_base.dat"));
  std::ostringstream stages;
  int last = -1;
  std::set<std::string> names;
  for (const auto& level : levels) {
    if (level.pack != "main" || level.name.empty() || level.name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos || !names.insert(level.name).second)
      throw std::runtime_error("Unexpected level name or source: " + level.name);
    if (level.stage != last) {
      if (last >= 0) stages << "}\n";
      last = level.stage;
      const std::string label = level.name == "hogger1hit" ? "WoW - Bonus" : "WoW - Stage " + std::to_string(last + 1);
      stages << "Stage\n{\n  Name: " << std::quoted(label) << '\n';
    }
    const auto name = "wow_" + level.name;
    stages << "  Level: " << name << ", " << std::quoted(level.title) << '\n';
    const auto path = "levels/" + name;
    files.emplace(path + ".dat", board(vfs, files, "levels/" + level.name + ".dat"));
    putText(files, path + ".ini", "base = _wow_base\n");
    for (const char* prefix : {"levels/", "levels/cached_thumbnails/"})
      if (!copyImage(vfs, files, prefix + level.name, prefix + name)) throw std::runtime_error("Missing board art: " + level.name);
  }
  stages << "}\n";
  putText(files, "levels/mod_levels.cfg", stages.str());
  putText(files, "mod.ini", "id = wow_levels\nname = Peggle WoW Levels\nversion = 1.0\ndescription = Ten campaign boards and one bonus layout from Peggle WoW\n");

  fs::create_directories(out.parent_path());
  if (!fs::create_directory(out)) throw std::runtime_error("The output folder already exists");
  try {
    for (const auto& [key, bytes] : files) {
      const auto file = out / pp::pathFromUtf8(key);
      fs::create_directories(file.parent_path());
      std::ofstream f(file, std::ios::binary);
      f.exceptions(std::ios::failbit | std::ios::badbit);
      f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      f.close();
    }
  } catch (...) {
    std::error_code ec;
    fs::remove_all(out, ec);
    throw;
  }
  std::cout << "Created " << levels.size() << " WoW levels in " << pp::pathToUtf8(out) << '\n';
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
  if (argc != 3) {
    std::cerr << "Usage: pp_wow_pack WOW_GAME_FOLDER OUTPUT_MOD_FOLDER\n";
    return 2;
  }
  try {
    build(fs::absolute(argv[1]), fs::absolute(argv[2]));
  } catch (const std::exception& e) {
    std::cerr << "Could not build WoW pack: " << e.what() << '\n';
    return 1;
  }
}
