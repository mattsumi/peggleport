#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vfs.h"

namespace pp {

struct Image {
  int w = 0, h = 0;
  std::vector<uint8_t> rgba;
  float scale = 1;
  std::string layer;
};

bool decodeImage(const std::vector<uint8_t>& bytes, Image& out, std::string* err = nullptr);
bool decodeImage(const std::vector<uint8_t>& bytes, Image& out, std::string* err, bool limited);
std::optional<Image> loadGameImage(const Vfs& vfs, const std::string& path, const std::string& explicitMask = "");
std::optional<Image> loadGameImage(const Vfs& vfs, const std::string& path, const std::string& explicitMask, bool limited);
void upscale2(Image& im, int rows, int cols);

}
