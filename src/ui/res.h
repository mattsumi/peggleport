#pragma once
#include <map>
#include <string>
#include <vector>

#include "vfs.h"

namespace pp::ui {

struct ResImage {
  std::string id, path;
  int cols = 1, rows = 1;
  std::string alphaImage, alphaGrid;
};

struct ResFont {
  std::string id, path, ref, tags;
};

class Manifest {
 public:
  bool load(const Vfs& vfs);
  const ResImage* image(const std::string& id) const;
  const std::string* sound(const std::string& id) const;
  const ResFont* font(const std::string& id) const;
  const std::vector<std::string>& groupOrder() const { return groupOrder_; }
  const std::vector<std::string>& groupImages(const std::string& g) const;
  size_t imageCount() const { return images_.size(); }

 private:
  std::map<std::string, ResImage> images_;
  std::map<std::string, std::string> sounds_;
  std::map<std::string, ResFont> fonts_;
  std::map<std::string, std::vector<std::string>> groups_;
  std::vector<std::string> groupOrder_;
};

}
