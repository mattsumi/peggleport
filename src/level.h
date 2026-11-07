#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pp {

struct Node;
using NodeP = std::shared_ptr<Node>;

enum class VK : uint8_t { Null, Num, Str, Ptr, Ref, Child, Verts };
enum class Prim : uint8_t { F32, I32, U32, I16, U16, I8, U8 };

struct Val {
  VK k = VK::Null;
  Prim p = Prim::I32;
  uint32_t bits = 0;
  std::string s;
  NodeP n;
  int ref = 0;
  std::vector<float> verts;
  float f() const;
  int64_t i() const;
};

struct Node {
  int id = 0;
  int type = 0;
  std::map<std::string, Val> fields;
  std::map<std::string, bool> flags;
  std::map<std::string, uint32_t> spare;

  bool has(const std::string& k) const { return fields.count(k) != 0; }
  float f(const std::string& k, float d = 0) const;
  int64_t i(const std::string& k, int64_t d = 0) const;
  bool flag(const std::string& k) const;
  std::string str(const std::string& k) const;
  const Val* val(const std::string& k) const;
  void setF(const std::string& k, float v);
};

struct Level {
  int32_t version = 0;
  uint8_t mode = 0;
  std::vector<Val> entries;
  std::vector<uint8_t> trailing;
  std::vector<Node*> byId;
  const Node* resolve(const Val& v) const;
};

std::optional<Level> parseLevel(const std::vector<uint8_t>& data, std::string* err = nullptr, int maxVersion = 0x52);
std::vector<uint8_t> writeLevel(const Level& lv);

}
