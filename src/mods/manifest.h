#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pp::mods {

constexpr int kModApi = 1;

struct Manifest {
  std::string id, name, version = "0", author, description;
  int api = kModApi;
  bool net = false;
  bool fs = false;
  bool cosmetic = false;
  std::vector<std::string> scripts;
  std::string native;
  int memoryMb = 16;
  int budget = 200000;
  int priority = 0;
  std::string format;
  std::vector<std::string> warnings;
};

bool parseManifest(const std::string& text, bool json, const std::string& folderName, Manifest& m, std::string* err);
bool loadManifest(const std::filesystem::path& dir, Manifest& m, std::string* err);
std::string sanitizeId(const std::string& s);
std::filesystem::path resolveNative(const std::filesystem::path& dir, const Manifest& m);
bool safeRelPath(const std::string& rel);

uint64_t hashModDir(const std::filesystem::path& dir, std::vector<std::string>* files = nullptr);
std::string hex64(uint64_t v);
uint64_t fnvMix(uint64_t h, const void* p, size_t n);

bool extractZip(const std::filesystem::path& zip, const std::filesystem::path& outDir, std::string* err);
bool inflateRaw(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out, size_t expect);
bool readFile(const std::filesystem::path& p, std::string& out, size_t maxBytes = 64u << 20);

}
